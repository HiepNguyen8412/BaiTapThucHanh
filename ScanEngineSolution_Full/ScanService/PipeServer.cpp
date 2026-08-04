#include "PipeServer.h"
#include "JobManager.h"
#include "Logger.h"
#include "Telemetry.h"
#include "../Common/WinUtil.h"

#include <Sddl.h>
#include <chrono>
#include <sstream>

namespace
{
    JobPriority DecodePriority(std::uint32_t value)
    {
        if (value >= static_cast<std::uint32_t>(JobPriority::High)) return JobPriority::High;
        if (value == static_cast<std::uint32_t>(JobPriority::Normal)) return JobPriority::Normal;
        return JobPriority::Low;
    }
}

ClientSession::ClientSession(
    HANDLE pipe,
    std::uint64_t sessionId,
    JobManager& jobs,
    Telemetry& telemetry,
    Logger& logger)
    : pipe_(pipe),
      sessionId_(sessionId),
      jobs_(jobs),
      telemetry_(telemetry),
      logger_(logger)
{
}

ClientSession::~ClientSession()
{
    Stop();
    Join();
}

void ClientSession::Start()
{
    running_.store(true);
    thread_ = std::thread(&ClientSession::Run, this);
}

void ClientSession::Stop()
{
    running_.store(false);

    // Đánh thức RunEventLoop nếu nó đang chờ event.
    eventCv_.notify_all();

    if (pipe_ != INVALID_HANDLE_VALUE)
    {
        CancelIoEx(pipe_, nullptr);
        DisconnectNamedPipe(pipe_);
        CloseHandle(pipe_);

        pipe_ = INVALID_HANDLE_VALUE;
    }
}

void ClientSession::Join()
{
    if (thread_.joinable() && thread_.get_id() != std::this_thread::get_id())
    {
        thread_.join();
    }
}

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
        return false;
    }
    return true;
}

void ClientSession::SendError(
    std::uint64_t requestId,
    DWORD errorCode,
    const std::wstring& message)
{
    AvProtocol::TlvWriter writer;
    writer.AddU32(AvProtocol::FieldType::ErrorCode, errorCode);
    writer.AddWide(AvProtocol::FieldType::Message, message);
    Send(AvProtocol::MessageType::Error, requestId, writer);
}

void ClientSession::SendSnapshot(
    const JobSnapshot& snapshot,
    std::uint64_t requestId,
    AvProtocol::MessageType type)
{
    AvProtocol::TlvWriter writer;
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
    }
    Send(type, requestId, writer);
}

void ClientSession::SendJobEvent(
    const JobSnapshot& snapshot)
{
    // Không ghi Named Pipe trực tiếp từ worker thread.
    // Chỉ đưa snapshot vào hàng đợi.
    {
        std::lock_guard<std::mutex> lock(eventMutex_);

        eventQueue_.push(snapshot);
    }

    // Đánh thức thread của ClientSession.
    eventCv_.notify_one();
}

void ClientSession::RunEventLoop()
{
    while (running_.load())
    {
        JobSnapshot snapshot{};

        {
            std::unique_lock<std::mutex> lock(eventMutex_);

            eventCv_.wait(
                lock,
                [this]()
                {
                    return !running_.load() ||
                        !eventQueue_.empty();
                });

            if (!running_.load() &&
                eventQueue_.empty())
            {
                return;
            }

            snapshot = eventQueue_.front();
            eventQueue_.pop();
        }

        // Chỉ gửi event thuộc job mà phiên scan này đăng ký.
        if (subscribedJobId_ != 0 &&
            snapshot.jobId != subscribedJobId_)
        {
            continue;
        }

        SendSnapshot(
            snapshot,
            snapshot.jobId,
            AvProtocol::MessageType::Event);

        // Send() có thể đặt running_ thành false nếu pipe lỗi.
        if (!running_.load())
        {
            return;
        }

        const bool terminalState =
            snapshot.state == JobState::Completed ||
            snapshot.state == JobState::Failed ||
            snapshot.state == JobState::Cancelled;

        // Đã gửi kết quả cuối thì kết thúc phiên streaming.
        if (terminalState)
        {
            return;
        }
    }
}

bool ClientSession::PerformHandshake()
{
    AvProtocol::Message message;
    DWORD error = ERROR_SUCCESS;
    if (!AvProtocol::ReadMessage(pipe_, message, error)) return false;
    if (static_cast<AvProtocol::MessageType>(message.header.type) != AvProtocol::MessageType::Hello)
    {
        SendError(message.header.requestId, ERROR_INVALID_DATA, L"HELLO is required before other commands");
        return false;
    }

    AvProtocol::TlvReader reader(message.payload);
    std::string clientId;
    std::string version;
    std::uint32_t pid = 0;
    std::wstring user;
    if (!reader.GetUtf8(AvProtocol::FieldType::ClientId, clientId) ||
        !reader.GetU32(AvProtocol::FieldType::Pid, pid) ||
        !reader.GetWide(AvProtocol::FieldType::User, user) ||
        !reader.GetUtf8(AvProtocol::FieldType::Version, version))
    {
        SendError(message.header.requestId, ERROR_INVALID_DATA, L"Invalid HELLO payload");
        return false;
    }

    AvProtocol::TlvWriter writer;
    writer.AddU64(AvProtocol::FieldType::SessionId, sessionId_);
    writer.AddUtf8(AvProtocol::FieldType::ServerVersion, "1.0.0");
    writer.AddUtf8(
        AvProtocol::FieldType::Policy,
        "TLV v1; cache TTL=10m; overloaded=high-priority-only; entropy threshold=7.2");
    if (!Send(AvProtocol::MessageType::Welcome, message.header.requestId, writer))
    {
        return false;
    }

    std::wstringstream stream;
    stream << L"Client connected. session=" << sessionId_
        << L", pid=" << pid << L", user=" << user;
    logger_.Info(stream.str());
    return true;
}

void ClientSession::HandleScan(const AvProtocol::Message& message)
{
    AvProtocol::TlvReader reader(message.payload);
    std::wstring path;
    std::uint32_t priority = static_cast<std::uint32_t>(JobPriority::Normal);
    std::uint32_t timeoutMs = 0;
    if (!reader.GetWide(AvProtocol::FieldType::Path, path))
    {
        SendError(message.header.requestId, ERROR_INVALID_DATA, L"SCAN requires a path");
        return;
    }
    reader.GetU32(AvProtocol::FieldType::Priority, priority);
    reader.GetU32(AvProtocol::FieldType::TimeoutMs, timeoutMs);

    const auto jobId = jobs_.Submit(
        path,
        DecodePriority(priority),
        timeoutMs,
        shared_from_this());

    // Phiên kết nối này sẽ streaming riêng cho job vừa tạo.
    subscribedJobId_ = jobId;
    scanSession_ = true;

    // Gửi ACK trước, sau đó mới gửi các progress event.
    AvProtocol::TlvWriter writer;
    writer.AddU64(
        AvProtocol::FieldType::JobId,
        jobId);

    writer.AddU32(
        AvProtocol::FieldType::JobStatus,
        static_cast<std::uint32_t>(
            JobState::Pending));

    writer.AddWide(
        AvProtocol::FieldType::Message,
        L"SCAN accepted");

    if (!Send(
        AvProtocol::MessageType::Ack,
        message.header.requestId,
        writer))
    {
        return;
    }

    // Thread của ClientSession chịu trách nhiệm lấy event
    // trong queue và ghi xuống Named Pipe.
    RunEventLoop();
}

void ClientSession::HandleQuery(const AvProtocol::Message& message)
{
    AvProtocol::TlvReader reader(message.payload);
    std::uint64_t jobId = 0;
    if (!reader.GetU64(AvProtocol::FieldType::JobId, jobId))
    {HandleMessage(message);
        SendError(message.header.requestId, ERROR_INVALID_DATA, L"QUERY requires jobId");
        return;
    }
    JobSnapshot snapshot{};
    if (!jobs_.Query(jobId, snapshot))
    {
        SendError(message.header.requestId, ERROR_NOT_FOUND, L"Job not found");
        return;
    }
    SendSnapshot(snapshot, message.header.requestId, AvProtocol::MessageType::Event);
}

void ClientSession::HandleCancel(const AvProtocol::Message& message)
{
    AvProtocol::TlvReader reader(message.payload);
    std::uint64_t jobId = 0;
    if (!reader.GetU64(AvProtocol::FieldType::JobId, jobId))
    {
        SendError(message.header.requestId, ERROR_INVALID_DATA, L"CANCEL requires jobId");
        return;
    }
    JobSnapshot snapshot{};
    if (!jobs_.Cancel(jobId, snapshot))
    {
        SendError(message.header.requestId, ERROR_NOT_FOUND, L"Job not found");
        return;
    }
    AvProtocol::TlvWriter writer;
    writer.AddU64(AvProtocol::FieldType::JobId, jobId);
    writer.AddU32(AvProtocol::FieldType::JobStatus, static_cast<std::uint32_t>(snapshot.state));
    writer.AddWide(AvProtocol::FieldType::Message, L"Cancellation requested");
    Send(AvProtocol::MessageType::Ack, message.header.requestId, writer);
}

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

void ClientSession::HandleMessage(const AvProtocol::Message& message)
{
    switch (static_cast<AvProtocol::MessageType>(message.header.type))
    {
    case AvProtocol::MessageType::Scan:
        HandleScan(message);
        break;
    case AvProtocol::MessageType::Query:
        HandleQuery(message);
        break;
    case AvProtocol::MessageType::Cancel:
        HandleCancel(message);
        break;
    case AvProtocol::MessageType::Telemetry:
        HandleTelemetry(message);
        break;
    default:
        SendError(message.header.requestId, ERROR_INVALID_FUNCTION, L"Unsupported command");
        break;
    }
}

void ClientSession::Run()
{
    if (!PerformHandshake())
    {
        Stop();
        return;
    }

    while (running_.load())
    {
        AvProtocol::Message message;
        DWORD error = ERROR_SUCCESS;

        if (!AvProtocol::ReadMessage(
            pipe_,
            message,
            error))
        {
            break;
        }

        HandleMessage(message);

        // Phiên SCAN chỉ phục vụ một job streaming.
        // Khi RunEventLoop kết thúc thì job đã đi tới
        // COMPLETED, FAILED hoặc CANCELLED.
        if (scanSession_)
        {
            break;
        }
    }

    running_.store(false);
    eventCv_.notify_all();

    logger_.Info(
        L"Client session disconnected: " +
        std::to_wstring(sessionId_));
}

PipeServer::PipeServer(JobManager& jobs, Telemetry& telemetry, Logger& logger)
    : jobs_(jobs), telemetry_(telemetry), logger_(logger) {}

PipeServer::~PipeServer() { Stop(); }

bool PipeServer::Start()
{
    if (running_.exchange(true)) return true;
    acceptThread_ = std::thread(&PipeServer::AcceptLoop, this);
    return true;
}

void PipeServer::Stop()
{
    if (!running_.exchange(false)) return;

    // Wake a blocking ConnectNamedPipe call.
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

    std::vector<std::shared_ptr<ClientSession>> sessions;
    {
        std::lock_guard lock(sessionsMutex_);
        sessions.swap(sessions_);
    }
    for (auto& session : sessions) session->Stop();
    for (auto& session : sessions) session->Join();
}

void PipeServer::AcceptLoop()
{
    logger_.Info(L"Named pipe server listening on " + std::wstring(AvProtocol::PIPE_NAME));
    while (running_.load())
    {
        PSECURITY_DESCRIPTOR securityDescriptor = nullptr;
        SECURITY_ATTRIBUTES securityAttributes{};
        securityAttributes.nLength = sizeof(securityAttributes);
        securityAttributes.bInheritHandle = FALSE;

        // LocalSystem/Admin: full access. Authenticated users: read/write.
        if (ConvertStringSecurityDescriptorToSecurityDescriptorW(
                L"D:(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;AU)",
                SDDL_REVISION_1,
                &securityDescriptor,
                nullptr))
        {
            securityAttributes.lpSecurityDescriptor = securityDescriptor;
        }

        HANDLE pipe = CreateNamedPipeW(
            AvProtocol::PIPE_NAME,
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            64u * 1024u,
            64u * 1024u,
            0,
            securityDescriptor != nullptr ? &securityAttributes : nullptr);
        if (securityDescriptor != nullptr)
        {
            LocalFree(securityDescriptor);
        }
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

        auto session = std::make_shared<ClientSession>(
            pipe,
            nextSessionId_++,
            jobs_,
            telemetry_,
            logger_);
        {
            std::lock_guard lock(sessionsMutex_);
            sessions_.push_back(session);
        }
        session->Start();
    }
}
