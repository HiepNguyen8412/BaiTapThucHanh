#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

class RateLimiter
{
public:
    RateLimiter(double requestsPerSecond = 10.0, double burst = 20.0);
    bool Allow(const std::string& clientKey, std::uint32_t& retryAfterMs);
    void Cleanup();

private:
    struct Bucket
    {
        double tokens{};
        std::chrono::steady_clock::time_point updated{};
        std::chrono::steady_clock::time_point lastSeen{};
    };

    double rate_;
    double burst_;
    std::mutex mutex_;
    std::unordered_map<std::string, Bucket> buckets_;
};
