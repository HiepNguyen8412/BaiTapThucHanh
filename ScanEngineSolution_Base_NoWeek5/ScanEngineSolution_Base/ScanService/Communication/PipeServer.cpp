// ============================================================================
// MODULE : ScanService / Communication
// ROLE   : HELLO + SCAN/QUERY/CANCEL/TELEMETRY handlers over Named Pipe.
// ============================================================================

#include "Communication/PipeServer.h"
#include "Jobs/JobManager.h"
#include "Monitoring/Logger.h"
#include "Monitoring/Telemetry.h"
#include "Platform/WinUtil.h"

#include <chrono>
#include <sstream>

namespace
{
    std::atomic_uint64_t g_nextSessionId{1};

    JobPriority DecodePriority(std::uint32_t value)
    {
        if (value >= static_cast<std::uint32_t>(JobPriority::High)) return JobPriority::High;
        if (value == static_cast<std::uint32_t>(JobPriority::Normal)) return JobPriority::Normal;
        return JobPriority::Low;
    }
}

ClientSession::ClientSession(HANDLE pipe, JobManager& jobs, Telemetry& telemetry, Logger& logger)
    : pipe_(pipe), jobs_(jobs), telemetry_(telemetry), logger_(logger)
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
    if (pipe_ != INVALID_HANDLE_VALUE)
    {
        CancelIoEx(pipe_, nullptr);
        DisconnectNamedPipe(pipe_);
    }
}

void ClientSession::Join()
{
    if (thread_.joinable() && thread_.get_id() != std::this_thread::get_id())
        thread_.join();

    if (pipe_ != INVALID_HANDLE_VALUE)
    {
        CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
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
    AvProtocol::ServiceErrorCode errorCode,
    const std::wstring& message)
{
    AvProtocol::TlvWriter writer;
    writer.AddU32(AvProtocol::FieldType::ErrorCode, static_cast<std::uint32_t>(errorCode));
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

void ClientSession::SendJobEvent(const JobSnapshot& snapshot)
{
    if (!running_.load()) return;
    SendSnapshot(snapshot, snapshot.jobId, AvProtocol::MessageType::Event);
}

bool ClientSession::PerformHandshake()
{
    AvProtocol::Message message;
    DWORD error = ERROR_SUCCESS;
    if (!AvProtocol::ReadMessage(pipe_, message, error)) return false;

    if (static_cast<AvProtocol::MessageType>(message.header.type) != AvProtocol::MessageType::Hello)
    {
        SendError(message.header.requestId,
            AvProtocol::ServiceErrorCode::ProtocolUnexpectedMessage,
            L"HELLO is required before other commands");
        return false;
    }

    AvProtocol::TlvReader reader(message.payload);
    if (!reader.IsValid())
    {
        SendError(message.header.requestId,
            AvProtocol::ServiceErrorCode::ProtocolMalformedTlv,
            L"Malformed HELLO payload");
        return false;
    }

    std::string clientId;
    std::string version;
    std::uint32_t pid = 0;
    std::wstring user;
    if (!reader.GetUtf8(AvProtocol::FieldType::ClientId, clientId) ||
        !reader.GetU32(AvProtocol::FieldType::Pid, pid) ||
        !reader.GetWide(AvProtocol::FieldType::User, user) ||
        !reader.GetUtf8(AvProtocol::FieldType::Version, version))
    {
        SendError(message.header.requestId,
            AvProtocol::ServiceErrorCode::InvalidRequest,
            L"HELLO requires clientId, pid, user and version");
        return false;
    }

    sessionId_ = g_nextSessionId++;

    AvProtocol::TlvWriter writer;
    writer.AddU64(AvProtocol::FieldType::SessionId, sessionId_);
    writer.AddUtf8(AvProtocol::FieldType::ServerVersion, "1.0.0");
    writer.AddWide(AvProtocol::FieldType::Policy, L"Basic scan service");
    if (!Send(AvProtocol::MessageType::Welcome, message.header.requestId, writer)) return false;

    std::wstringstream stream;
    stream << L"Client connected. session=" << sessionId_
        << L", pid=" << pid << L", user=" << user;
    logger_.Info(stream.str());
    return true;
}

void ClientSession::HandleScan(const AvProtocol::Message& message)
{
    AvProtocol::TlvReader reader(message.payload);
    if (!reader.IsValid())
    {
        SendError(message.header.requestId,
            AvProtocol::ServiceErrorCode::ProtocolMalformedTlv,
            L"Malformed SCAN payload");
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

    const auto jobId = jobs_.Submit(
        path,
        DecodePriority(priority),
        timeoutMs,
        shared_from_this());

    AvProtocol::TlvWriter writer;
    writer.AddU64(AvProtocol::FieldType::JobId, jobId);
    writer.AddU32(AvProtocol::FieldType::JobStatus, static_cast<std::uint32_t>(JobState::Pending));
    writer.AddWide(AvProtocol::FieldType::Message, L"SCAN accepted");
    Send(AvProtocol::MessageType::Ack, message.header.requestId, writer);
}

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

void ClientSession::Run()
{
    if (!PerformHandshake())
    {
        running_.store(false);
        return;
    }

    while (running_.load())
    {
        AvProtocol::Message message;
        DWORD error = ERROR_SUCCESS;
        if (!AvProtocol::ReadMessage(pipe_, message, error)) break;
        HandleMessage(message);
    }

    running_.store(false);
    logger_.Info(L"Client disconnected. session=" + std::to_wstring(sessionId_));
}

PipeServer::PipeServer(JobManager& jobs, Telemetry& telemetry, Logger& logger)
    : jobs_(jobs), telemetry_(telemetry), logger_(logger)
{
}

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

void PipeServer::AcceptLoop()
{
    logger_.Info(L"Named pipe server listening on " + std::wstring(AvProtocol::PIPE_NAME));

    while (running_.load())
    {
        HANDLE pipe = CreateNamedPipeW(
            AvProtocol::PIPE_NAME,
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            64u * 1024u,
            64u * 1024u,
            0,
            nullptr);

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

        auto connection = std::make_shared<ClientSession>(pipe, jobs_, telemetry_, logger_);
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
