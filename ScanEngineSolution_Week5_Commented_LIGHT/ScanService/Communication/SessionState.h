#pragma once

#include "Events/EventHistory.h"
#include "Events/EventQueue.h"
#include "Jobs/Job.h"
#include "Protocol/Protocol.h"
#include "Security/ClientIdentity.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// SessionState song doc lap voi ket noi pipe cu the: giu outbound queue + history de ho tro reconnect/resume.
class SessionState final : public IJobEventSink
{
public:
    SessionState(
        std::uint64_t sessionId,
        std::string clientId,
        ClientIdentity identity,
        std::size_t outboundLimit = 256,
        std::size_t historyLimit = 1024);

    void SendJobEvent(const JobSnapshot& snapshot) override;

    std::uint64_t Id() const noexcept { return sessionId_; }
    const std::string& ClientId() const noexcept { return clientId_; }
    const ClientIdentity& Identity() const noexcept { return identity_; }

    bool PrepareResume(
        const ClientIdentity& identity,
        std::uint64_t lastEventSeq,
        AvProtocol::ServiceErrorCode& errorCode,
        std::wstring& message);
    void MarkDisconnected();
    bool IsExpired(std::chrono::steady_clock::time_point now, std::chrono::seconds grace) const;
    bool IsConnected() const;

    bool WaitPop(ServiceEvent& event, const std::atomic_bool& connectionRunning);
    void Wake();

private:
    static EventImportance Classify(const JobSnapshot& snapshot) noexcept;
    void AppendHistoryLocked(const ServiceEvent& event);
    void QueueFlowControlLocked();

    std::uint64_t sessionId_{};
    std::string clientId_;
    ClientIdentity identity_;

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    EventQueue outbound_;
    EventHistory history_;
    std::uint64_t nextEventSeq_{1};
    std::uint64_t droppedVerbose_{0};
    bool flowControlQueued_{false};
    bool connected_{true};
    std::chrono::steady_clock::time_point disconnectedAt_{};
};
