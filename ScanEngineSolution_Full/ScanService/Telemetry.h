#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

struct TelemetrySnapshot
{
    std::uint64_t totalReceived{};
    std::uint64_t totalSucceeded{};
    std::uint64_t totalFailed{};
    std::uint64_t totalCancelled{};
    std::uint64_t cacheHits{};
    std::uint64_t pending{};
    std::uint64_t running{};
    double averageMs{};
    double p95Ms{};
};

class Telemetry
{
public:
    void JobReceived();
    void PendingIncrement();
    void PendingDecrement();
    void RunningIncrement();
    void RunningDecrement();
    void RecordSucceeded(std::uint64_t durationMs, bool cacheHit);
    void RecordFailed(std::uint64_t durationMs);
    void RecordCancelled(std::uint64_t durationMs);
    TelemetrySnapshot Snapshot() const;

private:
    void AddDuration(std::uint64_t durationMs);

    std::atomic_uint64_t totalReceived_{0};
    std::atomic_uint64_t totalSucceeded_{0};
    std::atomic_uint64_t totalFailed_{0};
    std::atomic_uint64_t totalCancelled_{0};
    std::atomic_uint64_t cacheHits_{0};
    std::atomic_uint64_t pending_{0};
    std::atomic_uint64_t running_{0};
    mutable std::mutex durationsMutex_;
    std::vector<std::uint64_t> durations_;
};
