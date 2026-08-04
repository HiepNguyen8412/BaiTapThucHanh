#pragma once

#include "../Common/EngineApi.h"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

struct CacheKey
{
    std::wstring normalizedPath;
    std::uint64_t lastWriteTime{};
    std::uint64_t fileSize{};

    bool operator==(const CacheKey& other) const noexcept
    {
        return normalizedPath == other.normalizedPath &&
            lastWriteTime == other.lastWriteTime &&
            fileSize == other.fileSize;
    }
};

struct CacheKeyHash
{
    std::size_t operator()(const CacheKey& key) const noexcept;
};

class ResultCache
{
public:
    explicit ResultCache(std::chrono::minutes ttl = std::chrono::minutes(10));
    bool TryGet(const CacheKey& key, EngineScanResultV1& result);
    void Put(const CacheKey& key, const EngineScanResultV1& result);
    void Cleanup();

private:
    struct Entry
    {
        EngineScanResultV1 result{};
        std::chrono::steady_clock::time_point expiresAt{};
    };

    std::chrono::minutes ttl_;
    std::mutex mutex_;
    std::unordered_map<CacheKey, Entry, CacheKeyHash> entries_;
};
