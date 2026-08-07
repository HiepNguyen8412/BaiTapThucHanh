// ============================================================================
// MODULE : ScanService / Jobs (CORE)
// ROLE   : Scheduler: job map, priority queue, worker pool, callback context.
// NOTE   : File duoc sap xep lai theo kien truc module de de doc va thuyet trinh.
// ============================================================================

#pragma once

#include "Jobs/Job.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>
#include <vector>

class EngineLoader;
class ResultCache;
class ThrottleMonitor;
class Telemetry;
class Logger;

class JobManager
{
public:
    JobManager(
        EngineLoader& engine,
        ResultCache& cache,
        ThrottleMonitor& throttle,
        Telemetry& telemetry,
        Logger& logger);
    ~JobManager();

    void Start(std::size_t workerCount);
    void Stop();
    std::uint64_t Submit(
        const std::wstring& path,
        JobPriority priority,
        std::uint32_t timeoutMs,
        const std::shared_ptr<IJobEventSink>& sink);
    bool Query(std::uint64_t jobId, JobSnapshot& snapshot) const;
    bool Cancel(std::uint64_t jobId, JobSnapshot& snapshot);

private:
    // Priority Queue policy:
    // High > Normal > Low; neu cung priority thi sequence nho hon di truoc (FIFO).
    struct QueueCompare
    {
        bool operator()(
            const std::shared_ptr<ScanJob>& left,
            const std::shared_ptr<ScanJob>& right) const
        {
            if (left->priority != right->priority)
            {
                return static_cast<std::uint32_t>(left->priority) <
                    static_cast<std::uint32_t>(right->priority);
            }
            return left->sequence > right->sequence;
        }
    };

    struct CallbackContext
    {
        JobManager* manager{};
        std::shared_ptr<ScanJob> job;
    };

    static BOOL WINAPI EngineCallback(
        const EngineProgressInfoV1* info,
        void* userContext);
    void WorkerLoop();
    void Notify(const std::shared_ptr<ScanJob>& job);
    void SetState(
        const std::shared_ptr<ScanJob>& job,
        JobState state,
        EngineStatus engineStatus,
        std::uint32_t progress,
        EngineScanStage stage,
        const std::wstring& message);
    void FinishCancelled(const std::shared_ptr<ScanJob>& job);

    EngineLoader& engine_;
    ResultCache& cache_;
    ThrottleMonitor& throttle_;
    Telemetry& telemetry_;
    Logger& logger_;

    std::atomic_bool running_{false};
    std::atomic_uint64_t nextJobId_{1};
    std::atomic_uint64_t nextSequence_{1};
    mutable std::mutex jobsMutex_;
    // jobs_: kho tra cuu tat ca Job de QUERY/CANCEL.
    std::unordered_map<std::uint64_t, std::shared_ptr<ScanJob>> jobs_;
    std::mutex queueMutex_;
    std::condition_variable queueCv_;
    // queue_: chi cac Job dang cho worker; thu tu do QueueCompare quyet dinh.
    std::priority_queue<
        std::shared_ptr<ScanJob>,
        std::vector<std::shared_ptr<ScanJob>>,
        QueueCompare> queue_;
    std::vector<std::thread> workers_;
};
