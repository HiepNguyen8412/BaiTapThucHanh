// ============================================================================
// MODULE : ScanService / Cache
// ROLE   : Week 5 hybrid cache: L1 memory + L2 persistent file.
// KEY    : path + mtime + size + engineVersion + ruleVersion + schemaVersion.
// ============================================================================

#pragma once

#include "Api/EngineApi.h"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

// Cache key khong chi la path: file thay doi hoac engine/rule/schema doi thi key cung doi -> MISS.
struct CacheKey
{
    std::wstring normalizedPath;
    std::uint64_t lastWriteTime{};
    std::uint64_t fileSize{};
    std::string engineVersion;
    std::uint32_t ruleSetVersion{1};
    std::uint32_t cacheSchemaVersion{1};

    bool operator==(const CacheKey& other) const noexcept
    {
        return normalizedPath == other.normalizedPath &&
            lastWriteTime == other.lastWriteTime &&
            fileSize == other.fileSize &&
            engineVersion == other.engineVersion &&
            ruleSetVersion == other.ruleSetVersion &&
            cacheSchemaVersion == other.cacheSchemaVersion;
    }
};

struct CacheKeyHash
{
    std::size_t operator()(const CacheKey& key) const noexcept;
};

// L1 = unordered_map trong RAM; L2 = file persistent. TTL va versioning ngan dung ket qua qua cu.
class ResultCache
{
public:
    explicit ResultCache(std::chrono::minutes ttl = std::chrono::hours(24 * 7));
    ~ResultCache();

    bool ConfigurePersistent(
        const std::wstring& filePath,
        const std::string& engineVersion,
        std::uint32_t ruleSetVersion = 1,
        std::uint32_t cacheSchemaVersion = 1);
    bool TryGet(const CacheKey& key, EngineScanResultV1& result);
    void Put(const CacheKey& key, const EngineScanResultV1& result);
    void Cleanup();
    void Flush();

private:
    struct Entry
    {
        EngineScanResultV1 result{};
        std::chrono::system_clock::time_point expiresAt{};
    };

    bool LoadLocked();
    bool SaveLocked();

    std::chrono::minutes ttl_;
    std::mutex mutex_;
    std::unordered_map<CacheKey, Entry, CacheKeyHash> entries_;
    std::wstring persistentPath_;
    std::string engineVersion_;
    std::uint32_t ruleSetVersion_{1};
    std::uint32_t cacheSchemaVersion_{1};
};
