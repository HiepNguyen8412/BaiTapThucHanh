// ============================================================================
// MODULE : ScanService / Monitoring
// ROLE   : Cap nhat va snapshot telemetry thread-safe.
// ============================================================================

#include "Monitoring/Telemetry.h"

#include <algorithm>
#include <numeric>

void Telemetry::JobReceived() { ++totalReceived_; }
void Telemetry::PendingIncrement() { ++pending_; }
void Telemetry::PendingDecrement() { if (pending_ > 0) --pending_; }
void Telemetry::RunningIncrement() { ++running_; }
void Telemetry::RunningDecrement() { if (running_ > 0) --running_; }

// Tang success, ghi cache hit neu co va dua duration vao mau latency.
void Telemetry::RecordSucceeded(std::uint64_t durationMs, bool cacheHit)
{
    ++totalSucceeded_;
    if (cacheHit) ++cacheHits_;
    AddDuration(durationMs);
}

// Tang failed va ghi thoi gian job that bai de thong ke latency van phan anh thuc te.
void Telemetry::RecordFailed(std::uint64_t durationMs)
{
    ++totalFailed_;
    AddDuration(durationMs);
}

// Tang cancelled va luu duration cua job bi huy.
void Telemetry::RecordCancelled(std::uint64_t durationMs)
{
    ++totalCancelled_;
    AddDuration(durationMs);
}

void Telemetry::AddDuration(std::uint64_t durationMs)
{
    std::lock_guard lock(durationsMutex_);
    if (durations_.size() >= 4096)
    {
        durations_.erase(durations_.begin(), durations_.begin() + 1024);
    }
    durations_.push_back(durationMs);
}

// Tao ban chup counter + average/p95 de logger va lenh TELEMETRY doc nhat quan.
TelemetrySnapshot Telemetry::Snapshot() const
{
    TelemetrySnapshot snapshot{};
    snapshot.totalReceived = totalReceived_.load();
    snapshot.totalSucceeded = totalSucceeded_.load();
    snapshot.totalFailed = totalFailed_.load();
    snapshot.totalCancelled = totalCancelled_.load();
    snapshot.cacheHits = cacheHits_.load();
    snapshot.pending = pending_.load();
    snapshot.running = running_.load();

    std::vector<std::uint64_t> copy;
    {
        std::lock_guard lock(durationsMutex_);
        copy = durations_;
    }
    if (!copy.empty())
    {
        const auto sum = std::accumulate(copy.begin(), copy.end(), std::uint64_t{0});
        snapshot.averageMs = static_cast<double>(sum) / static_cast<double>(copy.size());
        std::sort(copy.begin(), copy.end());
        const std::size_t index = static_cast<std::size_t>(0.95 * (copy.size() - 1));
        snapshot.p95Ms = static_cast<double>(copy[index]);
    }
    return snapshot;
}
