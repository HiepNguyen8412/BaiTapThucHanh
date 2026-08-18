#pragma once

#include "Communication/SessionState.h"
#include "Protocol/Protocol.h"
#include "Security/ClientIdentity.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

class Logger;

class SessionManager
{
public:
    explicit SessionManager(Logger& logger);
    ~SessionManager();

    void Start();
    void Stop();

    std::shared_ptr<SessionState> Create(
        const std::string& clientId,
        const ClientIdentity& identity);

    std::shared_ptr<SessionState> Resume(
        std::uint64_t sessionId,
        const std::string& clientId,
        const ClientIdentity& identity,
        std::uint64_t lastEventSeq,
        AvProtocol::ServiceErrorCode& errorCode,
        std::wstring& message);

    void MarkDisconnected(std::uint64_t sessionId);

private:
    void CleanupLoop();

    Logger& logger_;
    const std::chrono::seconds resumeWindow_{10};
    std::atomic_bool running_{false};
    std::atomic_uint64_t nextSessionId_{1};
    std::mutex mutex_;
    std::unordered_map<std::uint64_t, std::shared_ptr<SessionState>> sessions_;
    std::thread cleanupThread_;
};
