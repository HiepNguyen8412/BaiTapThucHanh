// ============================================================================
// MODULE : ScanService / Communication
// ROLE   : Named Pipe acceptor + one ClientSession per connected client.
// ============================================================================
#pragma once

#include "Jobs/Job.h"
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

class ClientSession final :
    public IJobEventSink,
    public std::enable_shared_from_this<ClientSession>
{
public:
    ClientSession(HANDLE pipe, JobManager& jobs, Telemetry& telemetry, Logger& logger);
    ~ClientSession();

    void Start();
    void Stop();
    void Join();
    bool IsRunning() const noexcept { return running_.load(); }

    void SendJobEvent(const JobSnapshot& snapshot) override;

private:
    void Run();
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
        const std::wstring& message);
    void SendSnapshot(
        const JobSnapshot& snapshot,
        std::uint64_t requestId,
        AvProtocol::MessageType type);

    HANDLE pipe_{INVALID_HANDLE_VALUE};
    JobManager& jobs_;
    Telemetry& telemetry_;
    Logger& logger_;
    std::uint64_t sessionId_{};
    std::atomic_bool running_{false};
    std::mutex sendMutex_;
    std::thread thread_;
};

class PipeServer
{
public:
    PipeServer(JobManager& jobs, Telemetry& telemetry, Logger& logger);
    ~PipeServer();
    bool Start();
    void Stop();

private:
    void AcceptLoop();

    JobManager& jobs_;
    Telemetry& telemetry_;
    Logger& logger_;
    std::atomic_bool running_{false};
    std::thread acceptThread_;
    std::mutex sessionsMutex_;
    std::vector<std::shared_ptr<ClientSession>> clientSessions_;
};
