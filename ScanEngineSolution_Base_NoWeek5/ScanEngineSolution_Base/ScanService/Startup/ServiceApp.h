// ============================================================================
// MODULE : ScanService / Startup
// ROLE   : Composition root: owns and connects all base Service modules.
// ============================================================================
#pragma once

#include "Cache/Cache.h"
#include "Communication/PipeServer.h"
#include "EngineBridge/EngineLoader.h"
#include "Jobs/JobManager.h"
#include "Monitoring/Logger.h"
#include "Monitoring/Telemetry.h"
#include "ResourceControl/Throttle.h"

#include <atomic>
#include <thread>

class ServiceApp
{
public:
    ServiceApp();
    ~ServiceApp();
    bool Start();
    void Stop();

private:
    void TelemetryLoop();

    Logger logger_;
    Telemetry telemetry_;
    ResultCache cache_;
    ThrottleMonitor throttle_;
    EngineLoader engine_;
    JobManager jobs_;
    PipeServer pipeServer_;
    std::atomic_bool running_{false};
    std::thread telemetryThread_;
};
