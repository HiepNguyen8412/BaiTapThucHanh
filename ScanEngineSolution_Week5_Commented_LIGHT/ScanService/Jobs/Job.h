// ============================================================================
// MODULE : ScanService / Jobs
// ROLE   : Mo hinh du lieu ScanJob, JobSnapshot va IJobEventSink.
// NOTE   : File duoc sap xep lai theo kien truc module de de doc va thuyet trinh.
// ============================================================================

#pragma once

#include "Api/EngineApi.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

// Interface dao nguoc dependency: JobManager khong can biet ClientSession, chi day snapshot vao event sink.
class IJobEventSink;

// Priority quyet dinh thu tu trong priority_queue: High > Normal > Low.
enum class JobPriority : std::uint32_t
{
    Low = 0,
    Normal = 1,
    High = 2
};

// Vong doi Job: Pending/Delayed -> Running -> mot trong Completed/Failed/Cancelled.
enum class JobState : std::uint32_t
{
    Pending = 0,
    Delayed,
    Running,
    Completed,
    Failed,
    Cancelled
};

// Ban sao read-only de gui ra ngoai JobManager. Snapshot tranh client/event thread cam mutex cua ScanJob lau.
struct JobSnapshot
{
    std::uint64_t jobId{};
    JobPriority priority{JobPriority::Normal};
    JobState state{JobState::Pending};
    std::uint32_t progress{};
    EngineScanStage stage{EngineScanStage::Starting};
    EngineStatus engineStatus{EngineStatus::Success};
    std::wstring path;
    std::wstring message;
    EngineScanResultV1 result{};
    bool hasResult{false};
    bool cacheHit{false};
};

class IJobEventSink
{
public:
    virtual ~IJobEventSink() = default;
    virtual void SendJobEvent(const JobSnapshot& snapshot) = 0;
};

// Trang thai song cua mot job trong Service. Mot so co dung atomic; phan state/result bao ve boi mutex.
struct ScanJob
{
    std::uint64_t id{};
    std::uint64_t sequence{};
    std::wstring path;
    JobPriority priority{JobPriority::Normal};
    std::uint32_t timeoutMs{};
    std::atomic_bool cancelRequested{false};
    std::atomic_uint64_t lastDelayedEventTick{0};
    std::weak_ptr<IJobEventSink> sink;

    mutable std::mutex mutex;
    JobState state{JobState::Pending};
    std::uint32_t progress{};
    EngineScanStage stage{EngineScanStage::Starting};
    EngineStatus engineStatus{EngineStatus::Success};
    std::wstring message;
    EngineScanResultV1 result{};
    bool hasResult{false};
    bool cacheHit{false};
    ULONGLONG startTick{};
};

// Chup ScanJob duoi mutex thanh JobSnapshot nhat quan truoc khi QUERY hoac streaming event.
inline JobSnapshot SnapshotOf(const ScanJob& job)
{
    std::lock_guard lock(job.mutex);
    JobSnapshot snapshot{};
    snapshot.jobId = job.id;
    snapshot.priority = job.priority;
    snapshot.state = job.state;
    snapshot.progress = job.progress;
    snapshot.stage = job.stage;
    snapshot.engineStatus = job.engineStatus;
    snapshot.path = job.path;
    snapshot.message = job.message;
    snapshot.result = job.result;
    snapshot.hasResult = job.hasResult;
    snapshot.cacheHit = job.cacheHit;
    return snapshot;
}
