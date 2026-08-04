#include "Cache.h"

std::size_t CacheKeyHash::operator()(const CacheKey& key) const noexcept
{
    std::size_t seed = std::hash<std::wstring>{}(key.normalizedPath);
    seed ^= std::hash<std::uint64_t>{}(key.lastWriteTime) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    seed ^= std::hash<std::uint64_t>{}(key.fileSize) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    return seed;
}

ResultCache::ResultCache(std::chrono::minutes ttl) : ttl_(ttl) {}

bool ResultCache::TryGet(const CacheKey& key, EngineScanResultV1& result)
{
    std::lock_guard lock(mutex_);
    const auto it = entries_.find(key);
    if (it == entries_.end()) return false;
    if (std::chrono::steady_clock::now() >= it->second.expiresAt)
    {
        entries_.erase(it);
        return false;
    }
    result = it->second.result;
    return true;
}

void ResultCache::Put(const CacheKey& key, const EngineScanResultV1& result)
{
    std::lock_guard lock(mutex_);
    entries_[key] = Entry{result, std::chrono::steady_clock::now() + ttl_};
}

void ResultCache::Cleanup()
{
    std::lock_guard lock(mutex_);
    const auto now = std::chrono::steady_clock::now();
    for (auto it = entries_.begin(); it != entries_.end();)
    {
        if (now >= it->second.expiresAt) it = entries_.erase(it);
        else ++it;
    }
}
