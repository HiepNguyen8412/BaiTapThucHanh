// ============================================================================
// MODULE : ScanService / Cache
// ROLE   : Hybrid cache: L1 memory (LRU) + L2 persistent SQLite database.
// KEY    : path + mtime + size + engineVersion + ruleVersion + schemaVersion.
// ============================================================================

#pragma once

#include "Api/EngineApi.h"

#include <chrono>
#include <cstdint>
#include <list>
#include <mutex>
#include <string>
#include <unordered_map>

struct sqlite3;
struct sqlite3_stmt;

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

// L1 = Memory LRU trong RAM; L2 = SQLite Database. TTL va versioning ngan dung ket qua qua cu.
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
        std::list<CacheKey>::iterator lruIt{};
    };

    void PutL1Locked(
        const CacheKey& key,
        const EngineScanResultV1& result,
        std::chrono::system_clock::time_point expiresAt);

    bool OpenDatabaseLocked();
    void CloseDatabaseLocked();
    bool QueryDatabaseLocked(
        const CacheKey& key,
        EngineScanResultV1& result,
        std::chrono::system_clock::time_point& expiresAt);
    bool InsertDatabaseLocked(
        const CacheKey& key,
        const EngineScanResultV1& result,
        std::chrono::system_clock::time_point expiresAt);
    void DeleteExpiredDatabaseLocked(std::chrono::system_clock::time_point now);

    static constexpr std::size_t L1_MAX_CAPACITY = 2000;

    std::chrono::minutes ttl_;
    std::mutex mutex_;
    std::unordered_map<CacheKey, Entry, CacheKeyHash> entries_;
    std::list<CacheKey> lruOrder_;

    std::wstring persistentPath_;
    std::string engineVersion_;
    std::uint32_t ruleSetVersion_{1};
    std::uint32_t cacheSchemaVersion_{1};

    sqlite3* db_{nullptr};
    sqlite3_stmt* stmtSelect_{nullptr};
    sqlite3_stmt* stmtInsert_{nullptr};
    sqlite3_stmt* stmtCleanup_{nullptr};
};
