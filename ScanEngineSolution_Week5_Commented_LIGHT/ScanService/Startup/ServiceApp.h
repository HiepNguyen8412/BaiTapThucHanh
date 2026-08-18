// ============================================================================
// MODULE : ScanService / Startup
// ROLE   : Composition root: owns and connects all Service modules.
// ============================================================================

#pragma once

#include "Cache/Cache.h"
#include "Communication/PipeServer.h"
#include "Communication/SessionManager.h"
#include "EngineBridge/EngineLoader.h"
#include "Jobs/JobManager.h"
#include "Monitoring/Logger.h"
#include "Monitoring/Telemetry.h"
#include "ResourceControl/Throttle.h"
#include "Security/PolicyManager.h"
#include "Security/RateLimiter.h"

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

    // Week 5 modules.
    SessionManager sessions_;
    PolicyManager policy_;
    RateLimiter rateLimiter_;

    PipeServer pipeServer_;
    std::atomic_bool running_{false};
    std::thread telemetryThread_;
};
