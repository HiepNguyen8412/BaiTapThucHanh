#include "Throttle.h"
#include "Logger.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <chrono>
#include <sstream>

ThrottleMonitor::ThrottleMonitor(Logger& logger) : logger_(logger) {}
ThrottleMonitor::~ThrottleMonitor() { Stop(); }

void ThrottleMonitor::Start()
{
    if (running_.exchange(true)) return;
    thread_ = std::thread(&ThrottleMonitor::MonitorLoop, this);
}

void ThrottleMonitor::Stop()
{
    running_.store(false);
    if (thread_.joinable()) thread_.join();
}

MachineSample ThrottleMonitor::Sample() const
{
    return MachineSample{cpuPercent_.load(), availableMemoryBytes_.load(), ioBytesPerSecond_.load()};
}

std::uint64_t ThrottleMonitor::FileTimeToUInt64(const FILETIME& value)
{
    ULARGE_INTEGER result{};
    result.HighPart = value.dwHighDateTime;
    result.LowPart = value.dwLowDateTime;
    return result.QuadPart;
}

void ThrottleMonitor::MonitorLoop()
{
    FILETIME idle{}, kernel{}, user{};
    GetSystemTimes(&idle, &kernel, &user);
    std::uint64_t previousIdle = FileTimeToUInt64(idle);
    std::uint64_t previousKernel = FileTimeToUInt64(kernel);
    std::uint64_t previousUser = FileTimeToUInt64(user);

    IO_COUNTERS io{};
    GetProcessIoCounters(GetCurrentProcess(), &io);
    std::uint64_t previousIo = io.ReadTransferCount + io.WriteTransferCount;
    auto previousTick = std::chrono::steady_clock::now();
    MachineState previousState = MachineState::Idle;

    while (running_.load())
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        double cpu = 0.0;
        if (GetSystemTimes(&idle, &kernel, &user))
        {
            const std::uint64_t currentIdle = FileTimeToUInt64(idle);
            const std::uint64_t currentKernel = FileTimeToUInt64(kernel);
            const std::uint64_t currentUser = FileTimeToUInt64(user);
            const std::uint64_t idleDelta = currentIdle - previousIdle;
            const std::uint64_t totalDelta = (currentKernel - previousKernel) + (currentUser - previousUser);
            if (totalDelta != 0)
            {
                cpu = 100.0 * static_cast<double>(totalDelta - idleDelta) /
                    static_cast<double>(totalDelta);
            }
            previousIdle = currentIdle;
            previousKernel = currentKernel;
            previousUser = currentUser;
        }

        MEMORYSTATUSEX memory{};
        memory.dwLength = sizeof(memory);
        GlobalMemoryStatusEx(&memory);

        GetProcessIoCounters(GetCurrentProcess(), &io);
        const std::uint64_t currentIo = io.ReadTransferCount + io.WriteTransferCount;
        const auto now = std::chrono::steady_clock::now();
        const double seconds = std::chrono::duration<double>(now - previousTick).count();
        const double ioRate = seconds > 0.0
            ? static_cast<double>(currentIo - previousIo) / seconds
            : 0.0;
        previousIo = currentIo;
        previousTick = now;

        cpuPercent_.store(cpu);
        availableMemoryBytes_.store(memory.ullAvailPhys);
        ioBytesPerSecond_.store(ioRate);

        constexpr std::uint64_t MiB = 1024ull * 1024ull;
        MachineState next = MachineState::Idle;
        if (cpu >= 85.0 || memory.ullAvailPhys < 512ull * MiB || ioRate >= 100.0 * MiB)
        {
            next = MachineState::Overloaded;
        }
        else if (cpu >= 60.0 || memory.ullAvailPhys < 1024ull * MiB || ioRate >= 30.0 * MiB)
        {
            next = MachineState::Busy;
        }
        state_.store(next);

        if (next != previousState)
        {
            std::wstringstream stream;
            stream << L"Throttle state=" << static_cast<std::uint32_t>(next)
                << L", CPU=" << cpu
                << L"%, available RAM=" << (memory.ullAvailPhys / MiB)
                << L" MiB, process IO=" << (ioRate / MiB) << L" MiB/s";
            logger_.Info(stream.str());
            previousState = next;
        }
    }
}
