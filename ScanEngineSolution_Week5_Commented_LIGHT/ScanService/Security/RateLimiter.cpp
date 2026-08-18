#include "Security/RateLimiter.h"

#include <algorithm>
#include <cmath>

RateLimiter::RateLimiter(double requestsPerSecond, double burst)
    : rate_(requestsPerSecond > 0.0 ? requestsPerSecond : 1.0),
      burst_(burst > 1.0 ? burst : 1.0)
{
}

// Token-bucket theo clientKey: moi request tieu token, token hoi phuc theo thoi gian.
// Het token => tu choi tam thoi va tra retryAfterMs de client biet luc nen thu lai.
bool RateLimiter::Allow(const std::string& clientKey, std::uint32_t& retryAfterMs)
{
    retryAfterMs = 0;
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard lock(mutex_);
    auto [it, inserted] = buckets_.try_emplace(clientKey);
    Bucket& bucket = it->second;
    if (inserted)
    {
        bucket.tokens = burst_;
        bucket.updated = now;
    }

    const std::chrono::duration<double> elapsed = now - bucket.updated;
    bucket.tokens = (std::min)(burst_, bucket.tokens + elapsed.count() * rate_);
    bucket.updated = now;
    bucket.lastSeen = now;

    if (bucket.tokens >= 1.0)
    {
        bucket.tokens -= 1.0;
        return true;
    }

    const double seconds = (1.0 - bucket.tokens) / rate_;
    retryAfterMs = static_cast<std::uint32_t>((std::max)(1.0, std::ceil(seconds * 1000.0)));
    return false;
}

// Xoa bucket cua client khong hoat dong lau de map rate-limit khong tang vo han.
void RateLimiter::Cleanup()
{
    const auto cutoff = std::chrono::steady_clock::now() - std::chrono::minutes(10);
    std::lock_guard lock(mutex_);
    for (auto it = buckets_.begin(); it != buckets_.end();)
    {
        if (it->second.lastSeen < cutoff) it = buckets_.erase(it);
        else ++it;
    }
}
