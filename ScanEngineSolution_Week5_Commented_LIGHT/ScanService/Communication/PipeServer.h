// ============================================================================
// MODULE : ScanService / Communication
// ROLE   : Named Pipe acceptor + physical ClientSession.
// WEEK 5 : SessionState survives a broken pipe; security/policy/rate are modules.
// ============================================================================

#pragma once

#include "Communication/SessionState.h"
#include "Protocol/Protocol.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

class JobManager;
class Telemetry;
class Logger;
class SessionManager;
class PolicyManager;
class RateLimiter;

class ClientSession final
{
public:
    ClientSession(
        HANDLE pipe,
        JobManager& jobs,
        Telemetry& telemetry,
        Logger& logger,
        SessionManager& sessions,
        PolicyManager& policy,
        RateLimiter& rateLimiter);
    ~ClientSession();

    void Start();
    void Stop();
    void Join();
    bool IsRunning() const noexcept { return running_.load(); }

private:
    void Run();
    void RunEventLoop();
    bool PerformHandshake();
    void HandleMessage(const AvProtocol::Message& message);
    void HandleScan(const AvProtocol::Message& message);
    void HandleQuery(const AvProtocol::Message& message);
    void HandleCancel(const AvProtocol::Message& message);
    void HandleTelemetry(const AvProtocol::Message& message);

    bool Send(
        AvProtocol::MessageType type,
        std::uint64_t requestId,
        const AvProtocol::TlvWriter& writer);
    void SendError(
        std::uint64_t requestId,
        AvProtocol::ServiceErrorCode errorCode,
        const std::wstring& message,
        std::uint32_t retryAfterMs = 0);
    void SendSnapshot(
        const JobSnapshot& snapshot,
        std::uint64_t requestId,
        AvProtocol::MessageType type,
        std::uint64_t eventSeq = 0);
    bool SendServiceEvent(const ServiceEvent& event);

    HANDLE pipe_{INVALID_HANDLE_VALUE};
    JobManager& jobs_;
    Telemetry& telemetry_;
    Logger& logger_;
    SessionManager& sessions_;
    PolicyManager& policy_;
    RateLimiter& rateLimiter_;

    std::shared_ptr<SessionState> sessionState_;
    std::uint64_t sessionId_{};
    std::atomic_bool running_{false};
    std::mutex sendMutex_;
    std::thread thread_;
    std::thread eventThread_;
};

class PipeServer
{
public:
    PipeServer(
        JobManager& jobs,
        Telemetry& telemetry,
        Logger& logger,
        SessionManager& sessions,
        PolicyManager& policy,
        RateLimiter& rateLimiter);
    ~PipeServer();
    bool Start();
    void Stop();

private:
    void AcceptLoop();

    JobManager& jobs_;
    Telemetry& telemetry_;
    Logger& logger_;
    SessionManager& sessionsManager_;
    PolicyManager& policy_;
    RateLimiter& rateLimiter_;
    std::atomic_bool running_{false};
    std::thread acceptThread_;
    std::mutex sessionsMutex_;
    std::vector<std::shared_ptr<ClientSession>> clientSessions_;
};
