#include "Cache.h"

using namespace std;

size_t CacheKeyHash::operator()(const CacheKey& key) const noexcept
{
    size_t seed = hash<wstring>{}(key.normalizedPath);
    seed ^= hash<uint64_t>{}(key.lastWriteTime) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    seed ^= hash<uint64_t>{}(key.fileSize) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    return seed;
}

ResultCache::ResultCache(chrono::minutes ttl) : ttl_(ttl) {}

bool ResultCache::TryGet(const CacheKey& key, EngineScanResultV1& result)
{
    lock_guard lock(mutex_);
    const auto it = entries_.find(key);
    if (it == entries_.end()) return false;
    if (chrono::steady_clock::now() >= it->second.expiresAt)
    {
        entries_.erase(it);
        return false;
    }
    result = it->second.result;
    return true;
}

void ResultCache::Put(const CacheKey& key, const EngineScanResultV1& result)
{
    lock_guard lock(mutex_);
    entries_[key] = Entry{result, chrono::steady_clock::now() + ttl_};
}

void ResultCache::Cleanup()
{
    lock_guard lock(mutex_);
    const auto now = chrono::steady_clock::now();
    for (auto it = entries_.begin(); it != entries_.end();)
    {
        if (now >= it->second.expiresAt) it = entries_.erase(it);
        else ++it;
    }
}
