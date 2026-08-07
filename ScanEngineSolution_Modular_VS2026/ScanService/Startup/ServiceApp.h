// ============================================================================
// MODULE : ScanService / Startup
// ROLE   : Composition root: so huu va noi cac module Service voi nhau.
// NOTE   : File duoc sap xep lai theo kien truc module de de doc va thuyet trinh.
// ============================================================================

#pragma once

#include "Cache/Cache.h"
#include "EngineBridge/EngineLoader.h"
#include "Jobs/JobManager.h"
#include "Monitoring/Logger.h"
#include "Communication/PipeServer.h"
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
