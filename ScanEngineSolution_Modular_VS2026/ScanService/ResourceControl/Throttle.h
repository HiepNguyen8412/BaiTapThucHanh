// ============================================================================
// MODULE : ScanService / ResourceControl
// ROLE   : Trang thai tai may: Idle / Busy / Overloaded.
// NOTE   : File duoc sap xep lai theo kien truc module de de doc va thuyet trinh.
// ============================================================================

#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <thread>

class Logger;

enum class MachineState : std::uint32_t
{
    Idle = 0,
    Busy,
    Overloaded
};

struct MachineSample
{
    double cpuPercent{};
    std::uint64_t availableMemoryBytes{};
    double processIoBytesPerSecond{};
};

class ThrottleMonitor
{
public:
    explicit ThrottleMonitor(Logger& logger);
    ~ThrottleMonitor();
    void Start();
    void Stop();
    MachineState State() const noexcept { return state_.load(); }
    MachineSample Sample() const;

private:
    void MonitorLoop();
    static std::uint64_t FileTimeToUInt64(const FILETIME& value);

    Logger& logger_;
    std::atomic_bool running_{false};
    std::atomic<MachineState> state_{MachineState::Idle};
    std::atomic<double> cpuPercent_{0.0};
    std::atomic_uint64_t availableMemoryBytes_{0};
    std::atomic<double> ioBytesPerSecond_{0.0};
    std::thread thread_;
};
