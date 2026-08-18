
// ============================================================================
// MODULE : ScanService / Jobs (CORE)
// ROLE   : Trai tim xu ly: Submit -> Queue -> WorkerLoop -> Cache -> DLL -> Callback.

// ============================================================================

#include "Jobs/JobManager.h"
#include "Cache/Cache.h"
#include "EngineBridge/EngineLoader.h"
#include "Monitoring/Logger.h"
#include "Monitoring/Telemetry.h"
#include "ResourceControl/Throttle.h"
#include "Platform/WinUtil.h"

#include <chrono>
#include <sstream>

JobManager::JobManager(
    EngineLoader& engine,
    ResultCache& cache,
    ThrottleMonitor& throttle,
    Telemetry& telemetry,
    Logger& logger)
    : engine_(engine),
      cache_(cache),
      throttle_(throttle),
      telemetry_(telemetry),
      logger_(logger)
{
}

JobManager::~JobManager() { Stop(); }

// Tao FIXED WORKER POOL. So worker co dinh, khong tao mot thread moi cho tung scan job.
void JobManager::Start(std::size_t workerCount)
{
    if (running_.exchange(true)) return;
    workerCount = workerCount == 0 ? 1 : workerCount;
    for (std::size_t i = 0; i < workerCount; ++i)
    {
        workers_.emplace_back(&JobManager::WorkerLoop, this);
    }
}

// Yeu cau cancel tat ca job, danh thuc worker dang cho queue va join tung worker.
void JobManager::Stop()
{
    {
        std::lock_guard lock(jobsMutex_);
        for (auto& [id, job] : jobs_)
        {
            UNREFERENCED_PARAMETER(id);
            job->cancelRequested.store(true);
        }
    }
    running_.store(false);
    queueCv_.notify_all();
    for (auto& worker : workers_)
    {
        if (worker.joinable()) worker.join();
    }
    workers_.clear();
}

// ---------------------------------------------------------------------------
// Submit() CHAY TREN CLIENT SESSION THREAD, khong phai worker thread.
// Nhiem vu: tao Job -> cap ID/sequence -> luu jobs_ -> push priority queue -> wake worker.
// ---------------------------------------------------------------------------
// TAO JOB: cap JobId + sequence, luu vao jobs_, day vao priority_queue va wake mot worker.
// Submit tra ve ngay cho ClientSession; scan that su dien ra bat dong bo trong WorkerLoop.
std::uint64_t JobManager::Submit(
    const std::wstring& path,
    JobPriority priority,
    std::uint32_t timeoutMs,
    const std::shared_ptr<IJobEventSink>& sink)
{
    auto job = std::make_shared<ScanJob>();
    job->id = nextJobId_++;
    job->sequence = nextSequence_++;
    job->path = path;
    job->priority = priority;
    job->timeoutMs = timeoutMs;
    job->sink = sink;
    job->message = L"Job queued";

    {
        std::lock_guard lock(jobsMutex_);
        jobs_[job->id] = job;
    }
    {
        std::lock_guard lock(queueMutex_);
        queue_.push(job);
    }
    telemetry_.JobReceived();
    telemetry_.PendingIncrement();
    queueCv_.notify_one();

    std::wstringstream stream;
    stream << L"Received job " << job->id << L" path=" << path;
    logger_.Info(stream.str());
    return job->id;
}

// Tim job trong map va tao snapshot thread-safe de tra ve client.
bool JobManager::Query(std::uint64_t jobId, JobSnapshot& snapshot) const
{
    std::shared_ptr<ScanJob> job;
    {
        std::lock_guard lock(jobsMutex_);
        const auto it = jobs_.find(jobId);
        if (it == jobs_.end()) return false;
        job = it->second;
    }
    snapshot = SnapshotOf(*job);
    return true;
}

// Dat atomic cancelRequested. Neu job chua chay, worker se ket thuc no khi lay ra queue;
// neu dang scan, EngineCallback tra FALSE de Engine dung som.
bool JobManager::Cancel(std::uint64_t jobId, JobSnapshot& snapshot)
{
    std::shared_ptr<ScanJob> job;
    {
        std::lock_guard lock(jobsMutex_);
        const auto it = jobs_.find(jobId);
        if (it == jobs_.end()) return false;
        job = it->second;
    }
    job->cancelRequested.store(true);
    {
        std::lock_guard lock(job->mutex);
        if (job->state == JobState::Pending || job->state == JobState::Delayed)
        {
            job->message = L"Cancellation requested";
        }
    }
    snapshot = SnapshotOf(*job);
    queueCv_.notify_all();
    return true;
}

// Day snapshot moi nhat sang IJobEventSink (SessionState) de stream ve dung client.
void JobManager::Notify(const std::shared_ptr<ScanJob>& job)
{
    if (auto sink = job->sink.lock())
    {
        sink->SendJobEvent(SnapshotOf(*job));
    }
}

// Cap nhat state/progress/stage/message duoi mutex, sau do Notify mot event ra client.
void JobManager::SetState(
    const std::shared_ptr<ScanJob>& job,
    JobState state,
    EngineStatus engineStatus,
    std::uint32_t progress,
    EngineScanStage stage,
    const std::wstring& message)
{
    {
        std::lock_guard lock(job->mutex);
        job->state = state;
        job->engineStatus = engineStatus;
        job->progress = progress;
        job->stage = stage;
        job->message = message;
    }
    Notify(job);
}

// Gom xu ly ket thuc CANCELLED ve mot cho va cap nhat telemetry thoi gian da chay.
void JobManager::FinishCancelled(const std::shared_ptr<ScanJob>& job)
{
    const std::uint64_t duration = job->startTick == 0 ? 0 : GetTickCount64() - job->startTick;
    SetState(
        job,
        JobState::Cancelled,
        EngineStatus::Cancelled,
        job->progress,
        EngineScanStage::Cancelled,
        L"Job cancelled");
    telemetry_.RecordCancelled(duration);
}

// CALLBACK DO ENGINE DLL GOI NGUOC VE SERVICE trong luc scan.
// Copy progress/result vao ScanJob -> Notify client; tra FALSE neu co cancelRequested.
BOOL WINAPI JobManager::EngineCallback(
    const EngineProgressInfoV1* info,
    void* userContext)
{
    if (info == nullptr || userContext == nullptr) return FALSE;
    auto* context = static_cast<CallbackContext*>(userContext);
    const auto& job = context->job;
    if (job->cancelRequested.load()) return FALSE;

    {
        std::lock_guard lock(job->mutex);
        job->progress = info->progressPercent;
        job->stage = info->stage;
        job->engineStatus = info->status;
        if (info->message != nullptr) job->message = info->message;
        if (info->eventType == EngineEventType::Result && info->result != nullptr)
        {
            job->result = *info->result;
            job->hasResult = true;
        }
    }
    context->manager->Notify(job);
    return job->cancelRequested.load() ? FALSE : TRUE;
}

// ---------------------------------------------------------------------------
// WorkerLoop() CHAY TREN CAC WORKER THREAD DUOC TAO SAN.
// Mot worker xu ly nhieu Job lien tiep; mot Job KHONG tao mot thread moi.
// Main path: Queue -> Cancel -> Throttle -> Cache -> EngineLoader -> final state.
// ---------------------------------------------------------------------------
// TRAI TIM XU LY JOB: wait priority queue -> cancel -> throttle -> cache -> EngineScanFile.
// Cache HIT di fast path; Cache MISS moi goi DLL va sau thanh cong se Put vao cache.
void JobManager::WorkerLoop()
{
    while (running_.load())
    {
        std::shared_ptr<ScanJob> job;
        {
            std::unique_lock lock(queueMutex_);
            queueCv_.wait(lock, [this] 
                { 
                    return !running_.load() || !queue_.empty(); 
                });
            if (!running_.load()) return;
            job = queue_.top();
            queue_.pop();
        }
        telemetry_.PendingDecrement();

        if (job->cancelRequested.load())
        {
            FinishCancelled(job);
            continue;
        }

        if (throttle_.State() == MachineState::Overloaded &&
            job->priority != JobPriority::High)
        {
            const ULONGLONG now = GetTickCount64();
            const ULONGLONG previous = job->lastDelayedEventTick.load();
            if (previous == 0 || now - previous >= 2000)
            {
                job->lastDelayedEventTick.store(now);
                SetState(
                    job,
                    JobState::Delayed,
                    EngineStatus::Success,
                    job->progress,
                    job->stage,
                    L"DELAYED: machine is overloaded; only high-priority jobs may run");
            }
            {
                std::lock_guard lock(queueMutex_);
                queue_.push(job);
            }
            telemetry_.PendingIncrement();
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }

        job->startTick = GetTickCount64();
        telemetry_.RunningIncrement();
        SetState(
            job,
            JobState::Running,
            EngineStatus::Success,
            0,
            EngineScanStage::Starting,
            L"Job running");

        DWORD pathError = ERROR_SUCCESS;
        const std::wstring normalizedPath = WinUtil::NormalizePath(job->path, pathError);
        std::uint64_t fileSize = 0;
        std::uint64_t lastWriteTime = 0;
        const bool identityOk = !normalizedPath.empty() &&
            WinUtil::GetFileIdentity(normalizedPath, fileSize, lastWriteTime, pathError);

        // Week 5 cache identity = file identity + engine/rule/schema version.
        // Updating ScanEngine.dll or the rule set naturally produces a MISS.
        constexpr std::uint32_t RULE_SET_VERSION = 1;
        constexpr std::uint32_t CACHE_SCHEMA_VERSION = 1;
        const std::string engineVersion = engine_.Version();
        CacheKey key{
            normalizedPath,
            lastWriteTime,
            fileSize,
            engineVersion,
            RULE_SET_VERSION,
            CACHE_SCHEMA_VERSION};
        EngineScanResultV1 cachedResult{};
        if (identityOk && cache_.TryGet(key, cachedResult))
        {
            cachedResult.scanDurationMs = 0;
            {
                std::lock_guard lock(job->mutex);
                job->state = JobState::Completed;
                job->engineStatus = EngineStatus::Success;
                job->progress = 100;
                job->stage = EngineScanStage::Completed;
                job->message = L"FAST PATH: cache hit";
                job->result = cachedResult;
                job->hasResult = true;
                job->cacheHit = true;
            }
            Notify(job);
            telemetry_.RunningDecrement();
            telemetry_.RecordSucceeded(0, true);
            continue;
        }

        EngineScanOptionsV1 options{};
        options.structSize = sizeof(options);
        options.apiVersion = ENGINE_API_VERSION_1;
        options.timeoutMs = job->timeoutMs;
        options.entropyThreshold = 7.2;
        options.maxEntropyBytes = 1024ull * 1024ull;
        options.progressIntervalMs = 100;

        // userContext cho callback biet event thuoc Job nao.
        CallbackContext context{this, job};
        const EngineStatus status = engine_.Scan(
            job->path.c_str(),
            &options,
            &JobManager::EngineCallback,
            &context);

        const std::uint64_t duration = GetTickCount64() - job->startTick;
        telemetry_.RunningDecrement();

        // Uu tien cancel truoc Success de tranh race: Client cancel dung luc DLL vua return.
        if (job->cancelRequested.load() || status == EngineStatus::Cancelled)
        {
            FinishCancelled(job);
        }
        else if (status == EngineStatus::Success)
        {
            EngineScanResultV1 result{};
            bool hasResult = false;
            {
                std::lock_guard lock(job->mutex);
                hasResult = job->hasResult;
                result = job->result;
                job->state = JobState::Completed;
                job->engineStatus = EngineStatus::Success;
                job->progress = 100;
                job->stage = EngineScanStage::Completed;
                job->message = L"Scan completed";
            }
            if (hasResult)
            {
                CacheKey finalKey{
                    normalizedPath,
                    result.lastWriteTime,
                    result.fileSize,
                    engineVersion,
                    RULE_SET_VERSION,
                    CACHE_SCHEMA_VERSION};
                if (!normalizedPath.empty()) cache_.Put(finalKey, result);
            }
            Notify(job);
            telemetry_.RecordSucceeded(duration, false);
        }
        else
        {
            SetState(
                job,
                JobState::Failed,
                status,
                job->progress,
                EngineScanStage::Failed,
                L"Scan failed");
            telemetry_.RecordFailed(duration);
        }

        cache_.Cleanup();
    }
}
