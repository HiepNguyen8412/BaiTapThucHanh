// ============================================================================
// MODULE : ScanService / Communication
// ROLE   : Named Pipe server va ClientSession; nhan command, stream Job event.
// NOTE   : File duoc sap xep lai theo kien truc module de de doc va thuyet trinh.
// ============================================================================

#pragma once

#include "Jobs/Job.h"
#include "Protocol/Protocol.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <queue>
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
    ClientSession(
        HANDLE pipe,
        std::uint64_t sessionId,
        JobManager& jobs,
        Telemetry& telemetry,
        Logger& logger);
    ~ClientSession();

    void Start();
    void Stop();
    void Join();
    void SendJobEvent(const JobSnapshot& snapshot) override;

private:
    // Thread doc request cua Client.
    void Run();

    // Thread gui event rieng: Worker chi push snapshot vao queue, khong bi block boi pipe I/O.
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
    void SendError(std::uint64_t requestId, DWORD errorCode, const std::wstring& message);
    void SendSnapshot(
        const JobSnapshot& snapshot,
        std::uint64_t requestId,
        AvProtocol::MessageType type);

    HANDLE pipe_{INVALID_HANDLE_VALUE};
    std::uint64_t sessionId_{};
    JobManager& jobs_;
    Telemetry& telemetry_;
    Logger& logger_;
    std::atomic_bool running_{false};
    // Bao ve WriteMessage: ACK/ERROR/EVENT khong duoc ghi xen ke byte.
    std::mutex sendMutex_;

    // Event queue tach callback/worker khoi Named Pipe I/O.
    std::mutex eventMutex_;
    std::condition_variable eventCv_;
    std::queue<JobSnapshot> eventQueue_;

    std::thread thread_;       // ClientSession::Run
    std::thread eventThread_;  // ClientSession::RunEventLoop
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
    std::atomic_uint64_t nextSessionId_{1};
    std::thread acceptThread_;
    std::mutex sessionsMutex_;
    std::vector<std::shared_ptr<ClientSession>> sessions_;
};
