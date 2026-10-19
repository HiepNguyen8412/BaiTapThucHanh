#include "Cache/Cache.h"
#include "ThirdParty/sqlite/sqlite3.h"

#include <cstring>
#include <filesystem>

namespace
{
    std::uint64_t ToUnixMs(std::chrono::system_clock::time_point time)
    {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            time.time_since_epoch()).count());
    }

    std::chrono::system_clock::time_point FromUnixMs(std::uint64_t value)
    {
        return std::chrono::system_clock::time_point(std::chrono::milliseconds(value));
    }
}

// Tron hash cua path + file identity + engine/rule/schema version de dung trong unordered_map L1.
std::size_t CacheKeyHash::operator()(const CacheKey& key) const noexcept
{
    std::size_t seed = std::hash<std::wstring>{}(key.normalizedPath);
    auto mix = [&](std::size_t value)
    {
        seed ^= value + 0x9e3779b9u + (seed << 6) + (seed >> 2);
    };
    mix(std::hash<std::uint64_t>{}(key.lastWriteTime));
    mix(std::hash<std::uint64_t>{}(key.fileSize));
    mix(std::hash<std::string>{}(key.engineVersion));
    mix(std::hash<std::uint32_t>{}(key.ruleSetVersion));
    mix(std::hash<std::uint32_t>{}(key.cacheSchemaVersion));
    return seed;
}

ResultCache::ResultCache(std::chrono::minutes ttl) : ttl_(ttl) {}

ResultCache::~ResultCache()
{
    std::lock_guard lock(mutex_);
    if (db_ != nullptr)
    {
        sqlite3_wal_checkpoint_v2(db_, nullptr, SQLITE_CHECKPOINT_PASSIVE, nullptr, nullptr);
    }
    CloseDatabaseLocked();
}

// Cau hinh SQLite persistent database va cac version tham gia invalidation.
bool ResultCache::ConfigurePersistent(
    const std::wstring& filePath,
    const std::string& engineVersion,
    std::uint32_t ruleSetVersion,
    std::uint32_t cacheSchemaVersion)
{
    std::lock_guard lock(mutex_);
    persistentPath_ = filePath;
    engineVersion_ = engineVersion;
    ruleSetVersion_ = ruleSetVersion;
    cacheSchemaVersion_ = cacheSchemaVersion;
    entries_.clear();
    lruOrder_.clear();

    // Don dep file ScanCache.dat cu (neu co) do da nang cap sang SQLite ScanCache.db
    std::error_code ec;
    const std::filesystem::path oldDat = std::filesystem::path(filePath).replace_extension(L".dat");
    if (std::filesystem::exists(oldDat, ec))
    {
        std::filesystem::remove(oldDat, ec);
    }

    return OpenDatabaseLocked();
}

// CACHE LOOKUP:
// 1) Tim L1 memory LRU cache truoc (sieu nhanh, khong cham disk).
// 2) Neu truot L1, query SQLite database (L2) qua B-Tree index.
// 3) Neu tim thay trong L2 va chua het han: Nap vao L1 LRU de hit nhanh lan sau.
bool ResultCache::TryGet(const CacheKey& key, EngineScanResultV1& result)
{
    std::lock_guard lock(mutex_);
    const auto now = std::chrono::system_clock::now();

    // 1) Tim trong L1 RAM
    auto it = entries_.find(key);
    if (it != entries_.end())
    {
        if (now >= it->second.expiresAt)
        {
            lruOrder_.erase(it->second.lruIt);
            entries_.erase(it);
            DeleteExpiredDatabaseLocked(now);
            return false;
        }

        // Hit L1: cap nhat vi tri len dau danh sach LRU
        lruOrder_.erase(it->second.lruIt);
        lruOrder_.push_front(key);
        it->second.lruIt = lruOrder_.begin();

        result = it->second.result;
        return true;
    }

    // 2) Tim trong L2 SQLite
    std::chrono::system_clock::time_point expiresAt{};
    if (QueryDatabaseLocked(key, result, expiresAt))
    {
        if (now >= expiresAt)
        {
            DeleteExpiredDatabaseLocked(now);
            return false;
        }

        // Hit L2: dua vao L1 RAM de truy xuat nhanh lan toi
        PutL1Locked(key, result, expiresAt);
        return true;
    }

    return false;
}

// Ghi ket qua scan thanh cong vao ca L1 memory LRU va L2 SQLite database.
void ResultCache::Put(const CacheKey& key, const EngineScanResultV1& result)
{
    std::lock_guard lock(mutex_);
    const auto expiresAt = std::chrono::system_clock::now() + ttl_;

    PutL1Locked(key, result, expiresAt);
    InsertDatabaseLocked(key, result, expiresAt);
}

// Dua phan tu vao L1 LRU cache; neu vuot qua L1_MAX_CAPACITY thi loai bo phan tu cu nhat.
void ResultCache::PutL1Locked(
    const CacheKey& key,
    const EngineScanResultV1& result,
    std::chrono::system_clock::time_point expiresAt)
{
    auto it = entries_.find(key);
    if (it != entries_.end())
    {
        lruOrder_.erase(it->second.lruIt);
        lruOrder_.push_front(key);
        it->second.result = result;
        it->second.expiresAt = expiresAt;
        it->second.lruIt = lruOrder_.begin();
        return;
    }

    if (entries_.size() >= L1_MAX_CAPACITY && !lruOrder_.empty())
    {
        const auto oldestKey = lruOrder_.back();
        entries_.erase(oldestKey);
        lruOrder_.pop_back();
    }

    lruOrder_.push_front(key);
    entries_[key] = Entry{result, expiresAt, lruOrder_.begin()};
}

// Xoa entry het TTL tren ca L1 RAM va L2 SQLite database.
void ResultCache::Cleanup()
{
    std::lock_guard lock(mutex_);
    const auto now = std::chrono::system_clock::now();

    for (auto it = entries_.begin(); it != entries_.end();)
    {
        if (now >= it->second.expiresAt)
        {
            lruOrder_.erase(it->second.lruIt);
            it = entries_.erase(it);
        }
        else
        {
            ++it;
        }
    }

    DeleteExpiredDatabaseLocked(now);
}

// Checkpoint WAL log xuong file SQLite DB chinh tren dia.
void ResultCache::Flush()
{
    std::lock_guard lock(mutex_);
    if (db_ != nullptr)
    {
        sqlite3_wal_checkpoint_v2(db_, nullptr, SQLITE_CHECKPOINT_PASSIVE, nullptr, nullptr);
    }
}

// Mo va khoi tao SQLite database, cau hinh PRAGMA va tao bang kem Prepared Statements.
bool ResultCache::OpenDatabaseLocked()
{
    CloseDatabaseLocked();
    if (persistentPath_.empty()) return true;

    std::error_code ec;
    const std::filesystem::path path(persistentPath_);
    if (path.has_parent_path())
    {
        std::filesystem::create_directories(path.parent_path(), ec);
    }

    const int rc = sqlite3_open16(persistentPath_.c_str(), &db_);
    if (rc != SQLITE_OK || db_ == nullptr)
    {
        CloseDatabaseLocked();
        return false;
    }

    sqlite3_busy_timeout(db_, 5000);
    sqlite3_exec(db_, "PRAGMA journal_mode = WAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(db_, "PRAGMA synchronous = NORMAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(db_, "PRAGMA temp_store = MEMORY;", nullptr, nullptr, nullptr);
    sqlite3_exec(db_, "PRAGMA cache_size = -2000;", nullptr, nullptr, nullptr);

    const char* createSql =
        "CREATE TABLE IF NOT EXISTS scan_cache ("
        "  normalized_path TEXT NOT NULL,"
        "  last_write_time INTEGER NOT NULL,"
        "  file_size INTEGER NOT NULL,"
        "  engine_version TEXT NOT NULL,"
        "  rule_set_version INTEGER NOT NULL,"
        "  cache_schema_version INTEGER NOT NULL,"
        "  expires_at INTEGER NOT NULL,"
        "  result_blob BLOB NOT NULL,"
        "  PRIMARY KEY (normalized_path, last_write_time, file_size, engine_version, rule_set_version, cache_schema_version)"
        ") WITHOUT ROWID;"
        "CREATE INDEX IF NOT EXISTS idx_cache_expires ON scan_cache(expires_at);";

    char* errMsg = nullptr;
    if (sqlite3_exec(db_, createSql, nullptr, nullptr, &errMsg) != SQLITE_OK)
    {
        if (errMsg != nullptr) sqlite3_free(errMsg);
        CloseDatabaseLocked();
        return false;
    }

    const char* selectSql =
        "SELECT expires_at, result_blob FROM scan_cache "
        "WHERE normalized_path = ?1 AND last_write_time = ?2 AND file_size = ?3 "
        "  AND engine_version = ?4 AND rule_set_version = ?5 AND cache_schema_version = ?6;";
    if (sqlite3_prepare_v2(db_, selectSql, -1, &stmtSelect_, nullptr) != SQLITE_OK)
    {
        CloseDatabaseLocked();
        return false;
    }

    const char* insertSql =
        "INSERT OR REPLACE INTO scan_cache ("
        "  normalized_path, last_write_time, file_size, engine_version, rule_set_version, cache_schema_version, expires_at, result_blob"
        ") VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8);";
    if (sqlite3_prepare_v2(db_, insertSql, -1, &stmtInsert_, nullptr) != SQLITE_OK)
    {
        CloseDatabaseLocked();
        return false;
    }

    const char* cleanupSql = "DELETE FROM scan_cache WHERE expires_at <= ?1;";
    if (sqlite3_prepare_v2(db_, cleanupSql, -1, &stmtCleanup_, nullptr) != SQLITE_OK)
    {
        CloseDatabaseLocked();
        return false;
    }

    // Tu dong xoa cac ban ghi cache neu engineVersion / ruleSetVersion / cacheSchemaVersion khong khop
    const char* purgeOldVersionSql =
        "DELETE FROM scan_cache "
        "WHERE engine_version != ?1 OR rule_set_version != ?2 OR cache_schema_version != ?3;";
    sqlite3_stmt* stmtPurge = nullptr;
    if (sqlite3_prepare_v2(db_, purgeOldVersionSql, -1, &stmtPurge, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_text(stmtPurge, 1, engineVersion_.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(stmtPurge, 2, static_cast<int>(ruleSetVersion_));
        sqlite3_bind_int(stmtPurge, 3, static_cast<int>(cacheSchemaVersion_));
        sqlite3_step(stmtPurge);
        sqlite3_finalize(stmtPurge);
    }

    return true;
}

// Dong prepared statements va ket noi SQLite database.
void ResultCache::CloseDatabaseLocked()
{
    if (stmtSelect_ != nullptr)
    {
        sqlite3_finalize(stmtSelect_);
        stmtSelect_ = nullptr;
    }
    if (stmtInsert_ != nullptr)
    {
        sqlite3_finalize(stmtInsert_);
        stmtInsert_ = nullptr;
    }
    if (stmtCleanup_ != nullptr)
    {
        sqlite3_finalize(stmtCleanup_);
        stmtCleanup_ = nullptr;
    }
    if (db_ != nullptr)
    {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

// Truy van ban ghi cache tu SQLite database.
bool ResultCache::QueryDatabaseLocked(
    const CacheKey& key,
    EngineScanResultV1& result,
    std::chrono::system_clock::time_point& expiresAt)
{
    if (stmtSelect_ == nullptr) return false;

    sqlite3_reset(stmtSelect_);
    sqlite3_clear_bindings(stmtSelect_);

    sqlite3_bind_text16(stmtSelect_, 1, key.normalizedPath.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmtSelect_, 2, static_cast<sqlite3_int64>(key.lastWriteTime));
    sqlite3_bind_int64(stmtSelect_, 3, static_cast<sqlite3_int64>(key.fileSize));
    sqlite3_bind_text(stmtSelect_, 4, key.engineVersion.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmtSelect_, 5, static_cast<int>(key.ruleSetVersion));
    sqlite3_bind_int(stmtSelect_, 6, static_cast<int>(key.cacheSchemaVersion));

    if (sqlite3_step(stmtSelect_) == SQLITE_ROW)
    {
        const auto expiresMs = static_cast<std::uint64_t>(sqlite3_column_int64(stmtSelect_, 0));
        expiresAt = FromUnixMs(expiresMs);

        const void* blobData = sqlite3_column_blob(stmtSelect_, 1);
        const int blobBytes = sqlite3_column_bytes(stmtSelect_, 1);
        if (blobData != nullptr && blobBytes == static_cast<int>(sizeof(EngineScanResultV1)))
        {
            std::memcpy(&result, blobData, sizeof(EngineScanResultV1));
            return true;
        }
    }

    return false;
}

// Ghi mot ban ghi cache vao SQLite database bang Prepared Statement.
bool ResultCache::InsertDatabaseLocked(
    const CacheKey& key,
    const EngineScanResultV1& result,
    std::chrono::system_clock::time_point expiresAt)
{
    if (stmtInsert_ == nullptr) return false;

    sqlite3_reset(stmtInsert_);
    sqlite3_clear_bindings(stmtInsert_);

    sqlite3_bind_text16(stmtInsert_, 1, key.normalizedPath.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmtInsert_, 2, static_cast<sqlite3_int64>(key.lastWriteTime));
    sqlite3_bind_int64(stmtInsert_, 3, static_cast<sqlite3_int64>(key.fileSize));
    sqlite3_bind_text(stmtInsert_, 4, key.engineVersion.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmtInsert_, 5, static_cast<int>(key.ruleSetVersion));
    sqlite3_bind_int(stmtInsert_, 6, static_cast<int>(key.cacheSchemaVersion));
    sqlite3_bind_int64(stmtInsert_, 7, static_cast<sqlite3_int64>(ToUnixMs(expiresAt)));
    sqlite3_bind_blob(stmtInsert_, 8, &result, static_cast<int>(sizeof(result)), SQLITE_STATIC);

    return sqlite3_step(stmtInsert_) == SQLITE_DONE;
}

// Xoa tat ca cac ban ghi da het han (TTL) khoi SQLite database bang index expires_at.
void ResultCache::DeleteExpiredDatabaseLocked(std::chrono::system_clock::time_point now)
{
    if (stmtCleanup_ == nullptr) return;

    sqlite3_reset(stmtCleanup_);
    sqlite3_clear_bindings(stmtCleanup_);
    sqlite3_bind_int64(stmtCleanup_, 1, static_cast<sqlite3_int64>(ToUnixMs(now)));
    sqlite3_step(stmtCleanup_);
    }
