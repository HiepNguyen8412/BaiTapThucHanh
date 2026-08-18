#pragma once

#include "../Common/EngineApi.h"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

using namespace std;

struct CacheKey
{
    wstring normalizedPath;
    uint64_t lastWriteTime{};
    uint64_t fileSize{};

    bool operator==(const CacheKey& other) const noexcept
    {
        return normalizedPath == other.normalizedPath &&
            lastWriteTime == other.lastWriteTime &&
            fileSize == other.fileSize;
    }
};

struct CacheKeyHash
{
    size_t operator()(const CacheKey& key) const noexcept;
};

class ResultCache
{
public:
    explicit ResultCache(chrono::minutes ttl = chrono::minutes(10));
    bool TryGet(const CacheKey& key, EngineScanResultV1& result);
    void Put(const CacheKey& key, const EngineScanResultV1& result);
    void Cleanup();

private:
    struct Entry
    {
        EngineScanResultV1 result{};
        chrono::steady_clock::time_point expiresAt{};
    };

    chrono::minutes ttl_;
    mutex mutex_;
    unordered_map<CacheKey, Entry, CacheKeyHash> entries_;
};
