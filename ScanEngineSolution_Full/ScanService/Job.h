#pragma once

#include "../Common/EngineApi.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

using namespace std;

class IJobEventSink;

enum class JobPriority : uint32_t
{
    Low = 0,
    Normal = 1,
    High = 2
};

enum class JobState : uint32_t
{
    Pending = 0,
    Delayed,
    Running,
    Completed,
    Failed,
    Cancelled
};

struct JobSnapshot
{
    uint64_t jobId{};
    JobPriority priority{JobPriority::Normal};
    JobState state{JobState::Pending};
    uint32_t progress{};
    EngineScanStage stage{EngineScanStage::Starting};
    EngineStatus engineStatus{EngineStatus::Success};
    wstring path;
    wstring message;
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

struct ScanJob
{
    uint64_t id{};
    uint64_t sequence{};
    wstring path;
    JobPriority priority{JobPriority::Normal};
    uint32_t timeoutMs{};
    atomic_bool cancelRequested{false};
    atomic_uint64_t lastDelayedEventTick{0};
    weak_ptr<IJobEventSink> sink;

    mutable mutex mutex;
    JobState state{JobState::Pending};
    uint32_t progress{};
    EngineScanStage stage{EngineScanStage::Starting};
    EngineStatus engineStatus{EngineStatus::Success};
    wstring message;
    EngineScanResultV1 result{};
    bool hasResult{false};
    bool cacheHit{false};
    ULONGLONG startTick{};
};

inline JobSnapshot SnapshotOf(const ScanJob& job)
{
    lock_guard lock(job.mutex);
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
