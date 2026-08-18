#pragma once

#include "Job.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>
#include <vector>

using namespace std;

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

    void Start(size_t workerCount);
    void Stop();
    uint64_t Submit(
        const wstring& path,
        JobPriority priority,
        uint32_t timeoutMs,
        const shared_ptr<IJobEventSink>& sink);
    bool Query(uint64_t jobId, JobSnapshot& snapshot) const;
    bool Cancel(uint64_t jobId, JobSnapshot& snapshot);

private:
    struct QueueCompare
    {
        bool operator()(
            const shared_ptr<ScanJob>& left,
            const shared_ptr<ScanJob>& right) const
        {
            if (left->priority != right->priority)
            {
                return static_cast<uint32_t>(left->priority) <
                    static_cast<uint32_t>(right->priority);
            }
            return left->sequence > right->sequence;
        }
    };

    struct CallbackContext
    {
        JobManager* manager{};
        shared_ptr<ScanJob> job;
    };

    static BOOL WINAPI EngineCallback(
        const EngineProgressInfoV1* info,
        void* userContext);
    void WorkerLoop();
    void Notify(const shared_ptr<ScanJob>& job);
    void SetState(
        const shared_ptr<ScanJob>& job,
        JobState state,
        EngineStatus engineStatus,
        uint32_t progress,
        EngineScanStage stage,
        const wstring& message);
    void FinishCancelled(const shared_ptr<ScanJob>& job);

    EngineLoader& engine_;
    ResultCache& cache_;
    ThrottleMonitor& throttle_;
    Telemetry& telemetry_;
    Logger& logger_;

    atomic_bool running_{false};
    atomic_uint64_t nextJobId_{1};
    atomic_uint64_t nextSequence_{1};
    mutable mutex jobsMutex_;
    unordered_map<uint64_t, shared_ptr<ScanJob>> jobs_;
    mutex queueMutex_;
    condition_variable queueCv_;
    priority_queue<
        shared_ptr<ScanJob>,
        vector<shared_ptr<ScanJob>>,
        QueueCompare> queue_;
    vector<thread> workers_;
};
