// ============================================================================
// MODULE : ScanService / Communication
// ROLE   : HELLO/RESUME + SCAN/QUERY/CANCEL/TELEMETRY handlers.
// WEEK 5 : ACL, real token authentication, policy/rate checks, resume events.
// ============================================================================

#include "Communication/PipeServer.h"
#include "Communication/SessionManager.h"
#include "Jobs/JobManager.h"
#include "Monitoring/Logger.h"
#include "Monitoring/Telemetry.h"
#include "Platform/WinUtil.h"
#include "Security/PipeSecurity.h"
#include "Security/PolicyManager.h"
#include "Security/RateLimiter.h"

#include <chrono>
#include <sstream>

namespace
{
    // Chuyen gia tri priority tu protocol (u32) sang enum noi bo va gioi han vao 3 muc hop le.
    JobPriority DecodePriority(std::uint32_t value)
    {
        if (value >= static_cast<std::uint32_t>(JobPriority::High)) return JobPriority::High;
        if (value == static_cast<std::uint32_t>(JobPriority::Normal)) return JobPriority::Normal;
        return JobPriority::Low;
    }
}

ClientSession::ClientSession(
    HANDLE pipe,
    JobManager& jobs,
    Telemetry& telemetry,
    Logger& logger,
    SessionManager& sessions,
    PolicyManager& policy,
    RateLimiter& rateLimiter)
    : pipe_(pipe),
      jobs_(jobs),
      telemetry_(telemetry),
      logger_(logger),
      sessions_(sessions),
      policy_(policy),
      rateLimiter_(rateLimiter)
{
}

ClientSession::~ClientSession()
{
    Stop();
    Join();
}

// Moi client co 1 ClientSession thread rieng de doc request ma khong chan AcceptLoop.
void ClientSession::Start()
{
    running_.store(true);
    thread_ = std::thread(&ClientSession::Run, this);
}

// Danh dau session dung, wake event queue va CancelIoEx de pha cac I/O dang block.
// Neu da handshake thi SessionState duoc giu lai mot thoi gian de ho tro RESUME.
void ClientSession::Stop()
{
    const bool wasRunning = running_.exchange(false);
    if (sessionState_ != nullptr)
    {
        sessionState_->Wake();
        if (wasRunning) sessions_.MarkDisconnected(sessionState_->Id());
    }
    if (pipe_ != INVALID_HANDLE_VALUE)
    {
        CancelIoEx(pipe_, nullptr);
        DisconnectNamedPipe(pipe_);
        CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
    }
}

// Cho thread doc request va thread gui event ket thuc truoc khi huy ClientSession.
void ClientSession::Join()
{
    if (thread_.joinable() && thread_.get_id() != std::this_thread::get_id()) thread_.join();
    if (eventThread_.joinable() && eventThread_.get_id() != std::this_thread::get_id()) eventThread_.join();
}

// Moi lan gui deu khoa sendMutex_ de hai thread khong ghi xen frame len cung Named Pipe.
bool ClientSession::Send(
    AvProtocol::MessageType type,
    std::uint64_t requestId,
    const AvProtocol::TlvWriter& writer)
{
    std::lock_guard lock(sendMutex_);
    if (!running_.load() || pipe_ == INVALID_HANDLE_VALUE) return false;
    DWORD error = ERROR_SUCCESS;
    if (!AvProtocol::WriteMessage(pipe_, type, requestId, writer, error))
    {
        running_.store(false);
        if (sessionState_ != nullptr) sessionState_->Wake();
        return false;
    }
    return true;
}

// Chuan hoa loi thanh MessageType::Error gom ErrorCode, Message va RetryAfterMs neu co.
void ClientSession::SendError(
    std::uint64_t requestId,
    AvProtocol::ServiceErrorCode errorCode,
    const std::wstring& message,
    std::uint32_t retryAfterMs)
{
    AvProtocol::TlvWriter writer;
    writer.AddU32(AvProtocol::FieldType::ErrorCode, static_cast<std::uint32_t>(errorCode));
    writer.AddWide(AvProtocol::FieldType::Message, message);
    if (retryAfterMs != 0) writer.AddU32(AvProtocol::FieldType::RetryAfterMs, retryAfterMs);
    Send(AvProtocol::MessageType::Error, requestId, writer);
}

// Chuyen JobSnapshot noi bo thanh cac field TLV de gui qua pipe.
// Neu job da co ket qua thi bo sung verdict/risk/PE metadata vao cung event.
void ClientSession::SendSnapshot(
    const JobSnapshot& snapshot,
    std::uint64_t requestId,
    AvProtocol::MessageType type,
    std::uint64_t eventSeq)
{
    AvProtocol::TlvWriter writer;
    if (eventSeq != 0) writer.AddU64(AvProtocol::FieldType::EventSeq, eventSeq);
    writer.AddU64(AvProtocol::FieldType::JobId, snapshot.jobId);
    writer.AddU32(AvProtocol::FieldType::Priority, static_cast<std::uint32_t>(snapshot.priority));
    writer.AddU32(AvProtocol::FieldType::JobStatus, static_cast<std::uint32_t>(snapshot.state));
    writer.AddU32(AvProtocol::FieldType::Progress, snapshot.progress);
    writer.AddU32(AvProtocol::FieldType::Stage, static_cast<std::uint32_t>(snapshot.stage));
    writer.AddU32(AvProtocol::FieldType::ErrorCode, static_cast<std::uint32_t>(snapshot.engineStatus));
    writer.AddWide(AvProtocol::FieldType::Path, snapshot.path);
    writer.AddWide(AvProtocol::FieldType::Message, snapshot.message);
    writer.AddBool(AvProtocol::FieldType::CacheHit, snapshot.cacheHit);

    if (snapshot.hasResult)
    {
        writer.AddU32(AvProtocol::FieldType::Verdict, static_cast<std::uint32_t>(snapshot.result.verdict));
        writer.AddU32(AvProtocol::FieldType::RiskScore, snapshot.result.riskScore);
        writer.AddU32(AvProtocol::FieldType::MatchedRules, snapshot.result.matchedRules);
        writer.AddU64(AvProtocol::FieldType::FileSize, snapshot.result.fileSize);
        writer.AddU64(AvProtocol::FieldType::LastWriteTime, snapshot.result.lastWriteTime);
        writer.AddDouble(AvProtocol::FieldType::Entropy, snapshot.result.entropy);
        writer.AddU64(AvProtocol::FieldType::DurationMs, snapshot.result.scanDurationMs);
        writer.AddU32(AvProtocol::FieldType::Win32Error, snapshot.result.win32Error);

        writer.AddBool(AvProtocol::FieldType::IsPe, snapshot.result.isPe != FALSE);
        writer.AddBool(AvProtocol::FieldType::IsPe32Plus, snapshot.result.isPe32Plus != FALSE);
        writer.AddU32(AvProtocol::FieldType::Machine, snapshot.result.machine);
        writer.AddU32(AvProtocol::FieldType::Subsystem, snapshot.result.subsystem);
        writer.AddBool(AvProtocol::FieldType::IsDll, snapshot.result.isDll != FALSE);
        writer.AddBool(AvProtocol::FieldType::IsDriver, snapshot.result.isDriver != FALSE);
        writer.AddBool(AvProtocol::FieldType::IsManaged, snapshot.result.isManaged != FALSE);
        writer.AddBool(AvProtocol::FieldType::IsSigned, snapshot.result.isSigned != FALSE);
        writer.AddU32(AvProtocol::FieldType::SignatureStatus, snapshot.result.signatureStatus);
        writer.AddBool(AvProtocol::FieldType::HasDebug, snapshot.result.hasDebug != FALSE);
        writer.AddBool(AvProtocol::FieldType::HasRichHeader, snapshot.result.hasRichHeader != FALSE);
        writer.AddU32(AvProtocol::FieldType::EntryPointRva, snapshot.result.entryPointRva);
        writer.AddU64(AvProtocol::FieldType::ImageBase, snapshot.result.imageBase);
        writer.AddU32(AvProtocol::FieldType::SectionCount, snapshot.result.sectionCount);
        writer.AddU64(AvProtocol::FieldType::OverlaySize, snapshot.result.overlaySize);
    }
    Send(type, requestId, writer);
}

// Gui event trong SessionState ra client. Event co sequence de client biet minh da nhan den dau.
bool ClientSession::SendServiceEvent(const ServiceEvent& event)
{
    if (event.kind == ServiceEventKind::FlowControl)
    {
        AvProtocol::TlvWriter writer;
        writer.AddU64(AvProtocol::FieldType::EventSeq, event.sequence);
        writer.AddU64(AvProtocol::FieldType::DroppedEvents, event.droppedVerbose);
        writer.AddU32(AvProtocol::FieldType::QueueDepth, event.queueDepth);
        writer.AddWide(AvProtocol::FieldType::Message, L"FLOW_CONTROL: verbose events were dropped because the client is slow");
        return Send(AvProtocol::MessageType::FlowControl, event.sequence, writer);
    }
    SendSnapshot(event.snapshot, event.snapshot.jobId, AvProtocol::MessageType::Event, event.sequence);
    return running_.load();
}

// Thread CHI de day streaming event tu EventQueue ra client.
// Tach khoi thread Run() de ket qua/progress van gui duoc trong khi Run dang cho request moi.
void ClientSession::RunEventLoop()
{
    while (running_.load() && sessionState_ != nullptr)
    {
        ServiceEvent event{};
        if (!sessionState_->WaitPop(event, running_)) break;
        if (!SendServiceEvent(event)) break;
    }
}

// BOC BAO MAT DAU TIEN: chi HELLO/RESUME moi duoc chap nhan truoc cac lenh khac.
// Service lay identity tu pipe token that, kiem tra client metadata va gan SessionState.
bool ClientSession::PerformHandshake()
{
    AvProtocol::Message message;
    DWORD readError = ERROR_SUCCESS;
    if (!AvProtocol::ReadMessage(pipe_, message, readError))
    {
        if (readError == ERROR_CRC)
        {
            SendError(message.header.requestId,
                AvProtocol::ServiceErrorCode::ProtocolChecksumFailed,
                L"Frame checksum failed");
        }
        return false;
    }

    const auto messageType = static_cast<AvProtocol::MessageType>(message.header.type);
    if (messageType != AvProtocol::MessageType::Hello && messageType != AvProtocol::MessageType::Resume)
    {
        SendError(message.header.requestId,
            AvProtocol::ServiceErrorCode::ProtocolUnexpectedMessage,
            L"HELLO or RESUME is required before other commands");
        return false;
    }

    AvProtocol::TlvReader reader(message.payload);
    if (!reader.IsValid())
    {
        SendError(message.header.requestId,
            AvProtocol::ServiceErrorCode::ProtocolMalformedTlv,
            L"Malformed handshake TLV payload");
        return false;
    }

    std::string clientId;
    std::string version;
    std::uint32_t pid = 0;
    std::wstring claimedUser;
    if (!reader.GetUtf8(AvProtocol::FieldType::ClientId, clientId) ||
        !reader.GetU32(AvProtocol::FieldType::Pid, pid) ||
        !reader.GetWide(AvProtocol::FieldType::User, claimedUser) ||
        !reader.GetUtf8(AvProtocol::FieldType::Version, version))
    {
        SendError(message.header.requestId,
            AvProtocol::ServiceErrorCode::InvalidRequest,
            L"Handshake requires clientId, pid, user and version");
        return false;
    }

    ClientIdentity identity{};
    AvProtocol::ServiceErrorCode authError{};
    std::wstring authMessage;
    if (!PipeSecurity::Authenticate(
        pipe_,
        pid, 
        claimedUser, 
        identity, 
        authError, 
        authMessage))
    {
        SendError(message.header.requestId, authError, authMessage);
        return false;
    }

    bool resumed = false;
    if (messageType == AvProtocol::MessageType::Resume)
    {
        std::uint64_t requestedSessionId = 0;
        std::uint64_t lastEventSeq = 0;
        if (!reader.GetU64(AvProtocol::FieldType::SessionId, requestedSessionId) ||
            !reader.GetU64(AvProtocol::FieldType::LastEventSeq, lastEventSeq))
        {
            SendError(message.header.requestId,
                AvProtocol::ServiceErrorCode::InvalidRequest,
                L"RESUME requires sessionId and lastEventSeq");
            return false;
        }
        AvProtocol::ServiceErrorCode resumeError{};
        std::wstring resumeMessage;
        sessionState_ = sessions_.Resume(
            requestedSessionId, clientId, identity, lastEventSeq, resumeError, resumeMessage);
        if (sessionState_ == nullptr)
        {
            SendError(message.header.requestId, resumeError, resumeMessage);
            return false;
        }
        resumed = true;
    }
    else
    {
        sessionState_ = sessions_.Create(clientId, identity);
    }

    sessionId_ = sessionState_->Id();
    AvProtocol::TlvWriter writer;
    writer.AddU64(AvProtocol::FieldType::SessionId, sessionId_);
    writer.AddUtf8(AvProtocol::FieldType::ServerVersion, "2.0.0-week5");
    writer.AddUtf8(
        AvProtocol::FieldType::Policy,
        "TLV v2 + CRC32; resume=10s; outbound=256; deny=Windows\\System32; rate=10/s burst=20; L1+L2 cache");
    writer.AddWide(AvProtocol::FieldType::Message, resumed ? L"RESUMED" : L"WELCOME");
    if (!Send(AvProtocol::MessageType::Welcome, message.header.requestId, writer)) return false;

    std::wstringstream stream;
    stream << (resumed ? L"Client resumed. session=" : L"Client connected. session=")
        << sessionId_ << L", pid=" << identity.pid << L", user=" << identity.userName;
    logger_.Info(stream.str());
    return true;
}

// Xu ly SCAN: parse path/priority/timeout -> rate limit -> policy -> JobManager::Submit().
// Ham nay KHONG scan file truc tiep; viec nang duoc day sang worker pool.
void ClientSession::HandleScan(const AvProtocol::Message& message)
{
    AvProtocol::TlvReader reader(message.payload);
    if (!reader.IsValid())
    {
        SendError(message.header.requestId,
            AvProtocol::ServiceErrorCode::ProtocolMalformedTlv,
            L"Malformed SCAN TLV payload");
        return;
    }

    std::wstring path;
    std::uint32_t priority = static_cast<std::uint32_t>(JobPriority::Normal);
    std::uint32_t timeoutMs = 0;
    if (!reader.GetWide(AvProtocol::FieldType::Path, path))
    {
        SendError(message.header.requestId,
            AvProtocol::ServiceErrorCode::InvalidRequest,
            L"SCAN requires a path");
        return;
    }
    reader.GetU32(AvProtocol::FieldType::Priority, priority);
    reader.GetU32(AvProtocol::FieldType::TimeoutMs, timeoutMs);

    std::wstring normalizedPath;
    std::wstring policyReason;
    if (sessionState_ == nullptr ||
        !policy_.CanScan(
            sessionState_->Identity(), 
            path, 
            normalizedPath, 
            policyReason))
    {
        SendError(message.header.requestId,
            AvProtocol::ServiceErrorCode::PolicyPathDenied,
            policyReason.empty() ? L"Path denied by policy" : policyReason);
        return;
    }

    std::uint32_t retryAfterMs = 0;
    const std::string rateKey = sessionState_->ClientId() + "|" +
        std::string(sessionState_->Identity().userSid.begin(), sessionState_->Identity().userSid.end());
    if (!rateLimiter_.Allow(rateKey, retryAfterMs))
    {
        SendError(message.header.requestId,
            AvProtocol::ServiceErrorCode::RateLimited,
            L"Too many SCAN requests for this clientId",
            retryAfterMs);
        return;
    }

    const auto jobId = jobs_.Submit(
        normalizedPath,
        DecodePriority(priority),
        timeoutMs,
        sessionState_);

    AvProtocol::TlvWriter writer;
    writer.AddU64(AvProtocol::FieldType::JobId, jobId);
    writer.AddU32(AvProtocol::FieldType::JobStatus, static_cast<std::uint32_t>(JobState::Pending));
    writer.AddWide(AvProtocol::FieldType::Message, L"SCAN accepted");
    Send(AvProtocol::MessageType::Ack, message.header.requestId, writer);
}

// QUERY chi lay snapshot hien tai cua Job theo JobId, khong lam thay doi job.
void ClientSession::HandleQuery(const AvProtocol::Message& message)
{
    AvProtocol::TlvReader reader(message.payload);
    std::uint64_t jobId = 0;
    if (!reader.IsValid() || !reader.GetU64(AvProtocol::FieldType::JobId, jobId))
    {
        SendError(message.header.requestId,
            AvProtocol::ServiceErrorCode::InvalidRequest,
            L"QUERY requires jobId");
        return;
    }
    JobSnapshot snapshot{};
    if (!jobs_.Query(jobId, snapshot))
    {
        SendError(message.header.requestId,
            AvProtocol::ServiceErrorCode::JobNotFound,
            L"Job not found");
        return;
    }
    SendSnapshot(snapshot, message.header.requestId, AvProtocol::MessageType::Event);
}

// CANCEL dat cancelRequested cho Job. Worker/callback se thay co nay va dung an toan.
void ClientSession::HandleCancel(const AvProtocol::Message& message)
{
    AvProtocol::TlvReader reader(message.payload);
    std::uint64_t jobId = 0;
    if (!reader.IsValid() || !reader.GetU64(AvProtocol::FieldType::JobId, jobId))
    {
        SendError(message.header.requestId,
            AvProtocol::ServiceErrorCode::InvalidRequest,
            L"CANCEL requires jobId");
        return;
    }
    JobSnapshot snapshot{};
    if (!jobs_.Cancel(jobId, snapshot))
    {
        SendError(message.header.requestId,
            AvProtocol::ServiceErrorCode::JobNotFound,
            L"Job not found");
        return;
    }
    AvProtocol::TlvWriter writer;
    writer.AddU64(AvProtocol::FieldType::JobId, jobId);
    writer.AddU32(AvProtocol::FieldType::JobStatus, static_cast<std::uint32_t>(snapshot.state));
    writer.AddWide(AvProtocol::FieldType::Message, L"Cancellation requested");
    Send(AvProtocol::MessageType::Ack, message.header.requestId, writer);
}

// Chup telemetry hien tai va tra cac counter/pending/running/latency cho client.
void ClientSession::HandleTelemetry(const AvProtocol::Message& message)
{
    const auto snapshot = telemetry_.Snapshot();
    AvProtocol::TlvWriter writer;
    writer.AddU64(AvProtocol::FieldType::TotalReceived, snapshot.totalReceived);
    writer.AddU64(AvProtocol::FieldType::TotalSucceeded, snapshot.totalSucceeded);
    writer.AddU64(AvProtocol::FieldType::TotalFailed, snapshot.totalFailed);
    writer.AddU64(AvProtocol::FieldType::TotalCancelled, snapshot.totalCancelled);
    writer.AddU64(AvProtocol::FieldType::CacheHits, snapshot.cacheHits);
    writer.AddU64(AvProtocol::FieldType::Pending, snapshot.pending);
    writer.AddU64(AvProtocol::FieldType::Running, snapshot.running);
    writer.AddDouble(AvProtocol::FieldType::AverageMs, snapshot.averageMs);
    writer.AddDouble(AvProtocol::FieldType::P95Ms, snapshot.p95Ms);
    Send(AvProtocol::MessageType::Telemetry, message.header.requestId, writer);
}

// Dispatcher sau handshake: dua MessageType den dung handler SCAN/QUERY/CANCEL/TELEMETRY.
void ClientSession::HandleMessage(const AvProtocol::Message& message)
{
    switch (static_cast<AvProtocol::MessageType>(message.header.type))
    {
    case AvProtocol::MessageType::Scan: HandleScan(message); break;
    case AvProtocol::MessageType::Query: HandleQuery(message); break;
    case AvProtocol::MessageType::Cancel: HandleCancel(message); break;
    case AvProtocol::MessageType::Telemetry: HandleTelemetry(message); break;
    default:
        SendError(message.header.requestId,
            AvProtocol::ServiceErrorCode::ProtocolUnexpectedMessage,
            L"Unsupported command after handshake");
        break;
    }
}

// LUONG CHINH CUA MOT CLIENT SESSION: handshake -> start event thread -> doc request lien tuc.
// Khi ReadMessage fail/pipe dong thi Stop() session va ket thuc thread.
void ClientSession::Run()
{
    if (!PerformHandshake())
    {
        Stop();
        return;
    }

    eventThread_ = std::thread(&ClientSession::RunEventLoop, this);

    while (running_.load())
    {
        AvProtocol::Message message;
        DWORD error = ERROR_SUCCESS;
        if (!AvProtocol::ReadMessage(pipe_, message, error))
        {
            if (error == ERROR_CRC)
            {
                SendError(message.header.requestId,
                    AvProtocol::ServiceErrorCode::ProtocolChecksumFailed,
                    L"Frame checksum failed");
                continue; // Header+payload were consumed, so framing is recovered.
            }
            break;
        }
        HandleMessage(message);
    }

    running_.store(false);
    if (sessionState_ != nullptr)
    {
        sessionState_->Wake();
        sessions_.MarkDisconnected(sessionState_->Id());
    }
    logger_.Info(L"Client pipe disconnected; logical session retained for 10s: " +
        std::to_wstring(sessionId_));
}

PipeServer::PipeServer(
    JobManager& jobs,
    Telemetry& telemetry,
    Logger& logger,
    SessionManager& sessions,
    PolicyManager& policy,
    RateLimiter& rateLimiter)
    : jobs_(jobs),
      telemetry_(telemetry),
      logger_(logger),
      sessionsManager_(sessions),
      policy_(policy),
      rateLimiter_(rateLimiter)
{
}

PipeServer::~PipeServer() { Stop(); }

// Khoi dong AcceptLoop thread. Tu luc nay service bat dau san sang nhan client.
bool PipeServer::Start()
{
    if (running_.exchange(true)) return true;
    acceptThread_ = std::thread(&PipeServer::AcceptLoop, this);
    return true;
}

// Dung AcceptLoop va dong tat ca ClientSession dang song de khong con I/O tren pipe.
void PipeServer::Stop()
{
    if (!running_.exchange(false)) return;

    HANDLE wake = CreateFileW(
        AvProtocol::PIPE_NAME,
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr);
    if (wake != INVALID_HANDLE_VALUE) CloseHandle(wake);
    if (acceptThread_.joinable()) acceptThread_.join();

    std::vector<std::shared_ptr<ClientSession>> active;
    {
        std::lock_guard lock(sessionsMutex_);
        active.swap(clientSessions_);
    }
    for (auto& session : active) session->Stop();
    for (auto& session : active) session->Join();
}

// ACCEPT LOOP: tao Named Pipe instance -> cho ConnectNamedPipe -> tao ClientSession.
// Moi ket noi duoc tach sang session thread, vi the AcceptLoop quay lai nhan client tiep theo ngay.
void PipeServer::AcceptLoop()
{
    logger_.Info(L"Named pipe server listening on " + std::wstring(AvProtocol::PIPE_NAME));
    while (running_.load())
    {
        PipeSecurityDescriptor security;
        if (!security.Build())
        {
            logger_.Error(L"Cannot build Named Pipe security descriptor");
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        HANDLE pipe = CreateNamedPipeW(
            AvProtocol::PIPE_NAME,
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            64u * 1024u,
            64u * 1024u,
            0,
            security.Attributes());
        if (pipe == INVALID_HANDLE_VALUE)
        {
            logger_.Error(L"CreateNamedPipe failed: " + WinUtil::GetLastErrorMessage(GetLastError()));
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        const BOOL connected = ConnectNamedPipe(pipe, nullptr)
            ? TRUE
            : (GetLastError() == ERROR_PIPE_CONNECTED);
        if (!connected)
        {
            CloseHandle(pipe);
            if (!running_.load()) break;
            continue;
        }
        if (!running_.load())
        {
            DisconnectNamedPipe(pipe);
            CloseHandle(pipe);
            break;
        }

        auto connection = std::make_shared<ClientSession>(
            pipe,
            jobs_,
            telemetry_,
            logger_,
            sessionsManager_,
            policy_,
            rateLimiter_);
        {
            std::lock_guard lock(sessionsMutex_);
            for (auto it = clientSessions_.begin(); it != clientSessions_.end();)
            {
                if (!(*it)->IsRunning())
                {
                    (*it)->Join();
                    it = clientSessions_.erase(it);
                }
                else ++it;
            }
            clientSessions_.push_back(connection);
        }
        connection->Start();
    }
}
