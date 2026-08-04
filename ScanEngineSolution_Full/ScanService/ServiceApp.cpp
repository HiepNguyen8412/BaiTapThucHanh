#include "ServiceApp.h"
#include "../Common/WinUtil.h"

#include <algorithm>
#include <chrono>
#include <sstream>
#include <thread>

ServiceApp::ServiceApp()
    : cache_(std::chrono::minutes(10)),
      throttle_(logger_),
      engine_(logger_),
      jobs_(engine_, cache_, throttle_, telemetry_, logger_),
      pipeServer_(jobs_, telemetry_, logger_)
{
}

ServiceApp::~ServiceApp() { Stop(); }

bool ServiceApp::Start()
{
    if (running_.exchange(true)) return true;

    const std::wstring directory = WinUtil::GetExecutableDirectory();
    logger_.Open(WinUtil::JoinPath(directory, L"ScanService.log"));
    logger_.Info(L"AvScanService starting");

    if (!engine_.Load(WinUtil::JoinPath(directory, L"ScanEngine.dll")))
    {
        running_.store(false);
        return false;
    }

    throttle_.Start();
    const unsigned int hardware = std::thread::hardware_concurrency();
    const std::size_t workers = (std::max)(2u, (std::min)(hardware == 0 ? 4u : hardware, 8u));
    jobs_.Start(workers);
    if (!pipeServer_.Start())
    {
        jobs_.Stop();
        throttle_.Stop();
        engine_.Unload();
        running_.store(false);
        return false;
    }

    telemetryThread_ = std::thread(&ServiceApp::TelemetryLoop, this);
    logger_.Info(L"AvScanService started with " + std::to_wstring(workers) + L" workers");
    return true;
}

void ServiceApp::Stop()
{
    if (!running_.exchange(false)) return;
    pipeServer_.Stop();
    jobs_.Stop();
    throttle_.Stop();
    if (telemetryThread_.joinable()) telemetryThread_.join();

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
    logger_.Info(L"AvScanService stopped");
}

void ServiceApp::TelemetryLoop()
{
    while (running_.load())
    {
        for (int i = 0; i < 30 && running_.load(); ++i)
        {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        if (!running_.load()) break;

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
