// ============================================================================
// MODULE : ScanService / Startup
// ROLE   : Start/stop Engine, L1/L2 cache, SessionManager, workers and pipe.
// ============================================================================

#include "Startup/ServiceApp.h"
#include "Platform/WinUtil.h"

#include <algorithm>
#include <chrono>
#include <sstream>
#include <thread>

// Khoi tao va noi cac module voi nhau bang dependency reference.
// Thu tu trong initializer list cho thay JobManager dung Engine/Cache/Throttle/Telemetry.
ServiceApp::ServiceApp()
    : cache_(std::chrono::minutes(10)),
      throttle_(logger_),
      engine_(logger_),
      jobs_(engine_, cache_, throttle_, telemetry_, logger_),
      sessions_(logger_),
      policy_(),
      rateLimiter_(10.0, 20.0),
      pipeServer_(jobs_, telemetry_, logger_, sessions_, policy_, rateLimiter_)
{
}

ServiceApp::~ServiceApp() { Stop(); }

// DIEM KHOI DONG CHINH CUA SERVICE.
// Thu tu co chu y: Engine -> persistent cache -> sessions -> throttle/workers -> PipeServer.
// Pipe mo CUOI CUNG de client khong the vao khi cac dependency chua san sang.
bool ServiceApp::Start()
{
    if (running_.exchange(true)) return true;

    const std::wstring directory = WinUtil::GetExecutableDirectory();
    logger_.Open(WinUtil::JoinPath(directory, L"ScanService.log"));
    logger_.Info(L"AvScanService Week 5 starting");

    // 1) Engine must be ready before cache keys/workers use EngineVersion.
    if (!engine_.Load(WinUtil::JoinPath(directory, L"ScanEngine.dll")))
    {
        running_.store(false);
        return false;
    }

    // 2) L2 cache survives service restart. Versioned key invalidates old engine/rules.
    const std::string engineVersion = engine_.Version();
    if (!cache_.ConfigurePersistent(
            WinUtil::JoinPath(directory, L"ScanCache.dat"),
            engineVersion,
            1,
            1))
    {
        logger_.Info(L"Persistent cache could not be loaded; continuing with an empty cache");
    }

    // 3) SessionManager must exist before accepting connections/resume requests.
    sessions_.Start();

    // 4) Throttle monitor + fixed worker pool.
    throttle_.Start();
    const unsigned int hardware = std::thread::hardware_concurrency();
    const std::size_t workers = (std::max)(2u, (std::min)(hardware == 0 ? 4u : hardware, 8u));
    jobs_.Start(workers);

    // 5) PipeServer starts last so every dependency is ready when the first HELLO arrives.
    if (!pipeServer_.Start())
    {
        jobs_.Stop();
        throttle_.Stop();
        sessions_.Stop();
        cache_.Flush();
        engine_.Unload();
        running_.store(false);
        return false;
    }

    telemetryThread_ = std::thread(&ServiceApp::TelemetryLoop, this);
    logger_.Info(L"AvScanService Week 5 started with " + std::to_wstring(workers) + L" workers");
    return true;
}

// Tat he thong theo thu tu nguoc: dung nhan client moi, dung worker/session/throttle,
// flush cache + log telemetry cuoi cung, sau do moi unload Engine DLL.
void ServiceApp::Stop()
{
    if (!running_.exchange(false)) return;

    // Stop new connections first; jobs can still publish final/cancel events to SessionState.
    pipeServer_.Stop();
    jobs_.Stop();
    sessions_.Stop();
    throttle_.Stop();
    if (telemetryThread_.joinable()) telemetryThread_.join();

    cache_.Cleanup();
    cache_.Flush();

    const auto snapshot = telemetry_.Snapshot();
    std::wstringstream stream;
    stream << L"Final telemetry: received=" << snapshot.totalReceived
        << L", success=" << snapshot.totalSucceeded
        << L", failed=" << snapshot.totalFailed
        << L", cancelled=" << snapshot.totalCancelled
        << L", cacheHits=" << snapshot.cacheHits
        << L", avgMs=" << snapshot.averageMs
        << L", p95Ms=" << snapshot.p95Ms;
    logger_.Info(stream.str());

    engine_.Unload();
    logger_.Info(L"AvScanService Week 5 stopped");
}

// Thread nen dinh ky cleanup RateLimiter/Cache va ghi snapshot telemetry.
// Vong lap kiem tra running_ thuong xuyen de Service stop khong bi tre lau.
void ServiceApp::TelemetryLoop()
{
    while (running_.load())
    {
        for (int i = 0; i < 30 && running_.load(); ++i)
            std::this_thread::sleep_for(std::chrono::seconds(1));
        if (!running_.load()) break;

        rateLimiter_.Cleanup();
        cache_.Cleanup();
        const auto snapshot = telemetry_.Snapshot();
        std::wstringstream stream;
        stream << L"Telemetry: received=" << snapshot.totalReceived
            << L", success=" << snapshot.totalSucceeded
            << L", failed=" << snapshot.totalFailed
            << L", cancelled=" << snapshot.totalCancelled
            << L", cacheHits=" << snapshot.cacheHits
            << L", pending=" << snapshot.pending
            << L", running=" << snapshot.running
            << L", avgMs=" << snapshot.averageMs
            << L", p95Ms=" << snapshot.p95Ms;
        logger_.Info(stream.str());
    }
}
