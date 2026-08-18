#include "Communication/SessionState.h"

#include <utility>

SessionState::SessionState(
    std::uint64_t sessionId,
    std::string clientId,
    ClientIdentity identity,
    std::size_t outboundLimit,
    std::size_t historyLimit)
    : sessionId_(sessionId),
      clientId_(std::move(clientId)),
      identity_(std::move(identity)),
      outbound_(outboundLimit),
      history_(historyLimit)
{
}

// Phan loai event quan trong/verbose. Khi backpressure, verbose co the bi bo truoc.
EventImportance SessionState::Classify(const JobSnapshot& snapshot) noexcept
{
    switch (snapshot.state)
    {
    case JobState::Completed:
    case JobState::Failed:
    case JobState::Cancelled:
        return EventImportance::Critical;
    default:
        return EventImportance::Verbose;
    }
}

// Luu event vao EventHistory de co the replay sau khi client RESUME.
void SessionState::AppendHistoryLocked(const ServiceEvent& event)
{
    history_.Add(event);
}

// Tao FLOW_CONTROL event thong bao client rang queue bi day va da drop event verbose.
void SessionState::QueueFlowControlLocked()
{
    if (flowControlQueued_) return;

    ServiceEvent flow{};
    flow.sequence = nextEventSeq_++;
    flow.kind = ServiceEventKind::FlowControl;
    flow.importance = EventImportance::Critical;
    flow.droppedVerbose = droppedVerbose_;
    flow.queueDepth = static_cast<std::uint32_t>(outbound_.Size());
    AppendHistoryLocked(flow);
    outbound_.Push(flow); // critical: never dropped
    flowControlQueued_ = true;
}

// Nhan JobSnapshot tu JobManager, cap event sequence, luu history va day vao EventQueue.
// Day la diem noi giua worker thread va streaming thread cua client.
void SessionState::SendJobEvent(const JobSnapshot& snapshot)
{
    std::lock_guard lock(mutex_);
    ServiceEvent event{};
    event.kind = ServiceEventKind::Job;
    event.importance = Classify(snapshot);
    event.snapshot = snapshot;

    // Do not consume a sequence number for a verbose event that is dropped.
    if (event.importance == EventImportance::Verbose && outbound_.Size() >= outbound_.MaxSize())
    {
        ++droppedVerbose_;
        QueueFlowControlLocked();
        cv_.notify_all();
        return;
    }

    event.sequence = nextEventSeq_++;
    AppendHistoryLocked(event);
    outbound_.Push(event);
    cv_.notify_all();
}

// RESUME: xac minh session dang disconnect + dung principal, sau do replay event sau lastEventSeq.
// Neu history khong con du sequence client yeu cau thi resume bi tu choi de tranh mat event am tham.
bool SessionState::PrepareResume(
    const ClientIdentity& identity,
    std::uint64_t lastEventSeq,
    AvProtocol::ServiceErrorCode& errorCode,
    std::wstring& message)
{
    std::lock_guard lock(mutex_);
    if (connected_)
    {
        errorCode = AvProtocol::ServiceErrorCode::ResumeAlreadyConnected;
        message = L"Session is already connected";
        return false;
    }
    if (!identity_.SamePrincipal(identity))
    {
        errorCode = AvProtocol::ServiceErrorCode::ResumeIdentityMismatch;
        message = L"RESUME identity does not match the original session";
        return false;
    }

    std::vector<ServiceEvent> replay;
    if (!history_.GetAfter(lastEventSeq, replay))
    {
        errorCode = AvProtocol::ServiceErrorCode::ResumeNotFound;
        message = L"Requested event sequence is older than retained history";
        return false;
    }

    outbound_.Clear();
    flowControlQueued_ = false;
    for (const auto& event : replay)
    {
        outbound_.PushReplay(event);
        if (event.kind == ServiceEventKind::FlowControl) flowControlQueued_ = true;
    }
    connected_ = true;
    disconnectedAt_ = {};
    errorCode = AvProtocol::ServiceErrorCode::Ok;
    message = L"Session resumed";
    cv_.notify_all();
    return true;
}

// Ghi thoi diem disconnect; history van ton tai tam thoi cho chuc nang resume.
void SessionState::MarkDisconnected()
{
    std::lock_guard lock(mutex_);
    if (!connected_) return;
    connected_ = false;
    disconnectedAt_ = std::chrono::steady_clock::now();
    cv_.notify_all();
}

bool SessionState::IsExpired(
    std::chrono::steady_clock::time_point now,
    std::chrono::seconds grace) const
{
    std::lock_guard lock(mutex_);
    return !connected_ && disconnectedAt_.time_since_epoch().count() != 0 &&
        now - disconnectedAt_ > grace;
}

bool SessionState::IsConnected() const
{
    std::lock_guard lock(mutex_);
    return connected_;
}

// Event thread block tren condition_variable cho toi khi co event hoac connection dung.
bool SessionState::WaitPop(ServiceEvent& event, const std::atomic_bool& connectionRunning)
{
    std::unique_lock lock(mutex_);
    cv_.wait(lock, [&]
    {
        return !connectionRunning.load() || !outbound_.Empty();
    });
    if (!connectionRunning.load()) return false;
    if (!outbound_.TryPop(event)) return false;
    if (event.kind == ServiceEventKind::FlowControl)
    {
        flowControlQueued_ = false;
        if (droppedVerbose_ > event.droppedVerbose) QueueFlowControlLocked();
    }
    return true;
}

// Danh thuc WaitPop khi can shutdown/disconnect de thread khong bi treo.
void SessionState::Wake()
{
    cv_.notify_all();
}
