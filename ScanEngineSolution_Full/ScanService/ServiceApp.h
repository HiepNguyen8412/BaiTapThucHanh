#pragma once

#include "Cache.h"
#include "EngineLoader.h"
#include "JobManager.h"
#include "Logger.h"
#include "PipeServer.h"
#include "Telemetry.h"
#include "Throttle.h"

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
