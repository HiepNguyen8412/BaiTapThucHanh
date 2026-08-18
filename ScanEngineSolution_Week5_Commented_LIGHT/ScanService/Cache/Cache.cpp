#include "Cache/Cache.h"

#include <filesystem>
#include <fstream>
#include <limits>

namespace
{
    constexpr std::uint32_t CACHE_MAGIC = 0x35435641u; // 'AVC5'
    constexpr std::uint32_t FILE_FORMAT_VERSION = 1;
    constexpr std::uint32_t MAX_RECORDS = 100000;

    template <typename T>
    void WritePod(std::ostream& stream, const T& value)
    {
        stream.write(reinterpret_cast<const char*>(&value), sizeof(value));
    }

    template <typename T>
    bool ReadPod(std::istream& stream, T& value)
    {
        return static_cast<bool>(stream.read(reinterpret_cast<char*>(&value), sizeof(value)));
    }

    void WriteString(std::ostream& stream, const std::string& value)
    {
        const std::uint32_t size = static_cast<std::uint32_t>(value.size());
        WritePod(stream, size);
        if (size != 0) stream.write(value.data(), size);
    }

    bool ReadString(std::istream& stream, std::string& value)
    {
        std::uint32_t size = 0;
        if (!ReadPod(stream, size) || size > 64u * 1024u) return false;
        value.resize(size);
        return size == 0 || static_cast<bool>(stream.read(value.data(), size));
    }

    void WriteWide(std::ostream& stream, const std::wstring& value)
    {
        const std::uint32_t count = static_cast<std::uint32_t>(value.size());
        WritePod(stream, count);
        if (count != 0)
        {
            stream.write(reinterpret_cast<const char*>(value.data()),
                static_cast<std::streamsize>(count * sizeof(wchar_t)));
        }
    }

    bool ReadWide(std::istream& stream, std::wstring& value)
    {
        std::uint32_t count = 0;
        if (!ReadPod(stream, count) || count > 32768u) return false;
        value.resize(count);
        return count == 0 || static_cast<bool>(stream.read(
            reinterpret_cast<char*>(value.data()),
            static_cast<std::streamsize>(count * sizeof(wchar_t))));
    }

    std::uint64_t ToUnixMs(std::chrono::system_clock::time_point time)
    {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            time.time_since_epoch()).count());
    }

    std::chrono::system_clock::time_point FromUnixMs(std::uint64_t value)
    {
        return std::chrono::system_clock::time_point(std::chrono::milliseconds(value));
    }

    void WriteResult(std::ostream& out, const EngineScanResultV1& r)
    {
        WritePod(out, r.verdict);
        WritePod(out, r.riskScore);
        WritePod(out, r.matchedRules);
        WritePod(out, r.win32Error);
        WritePod(out, r.fileSize);
        WritePod(out, r.lastWriteTime);
        WritePod(out, r.entropy);
        WritePod(out, r.scanDurationMs);
        std::wstring description(r.description);
        WriteWide(out, description);

        WritePod(out, r.isPe);
        WritePod(out, r.isPe32Plus);
        WritePod(out, r.machine);
        WritePod(out, r.subsystem);
        WritePod(out, r.isDll);
        WritePod(out, r.isDriver);
        WritePod(out, r.isManaged);
        WritePod(out, r.isSigned);
        WritePod(out, r.signatureStatus);
        WritePod(out, r.hasDebug);
        WritePod(out, r.hasRichHeader);
        WritePod(out, r.entryPointRva);
        WritePod(out, r.imageBase);
        WritePod(out, r.sectionCount);
        WritePod(out, r.overlaySize);
    }

    bool ReadResult(std::istream& in, EngineScanResultV1& r)
    {
        r = {};
        r.structSize = sizeof(r);
        r.apiVersion = ENGINE_API_VERSION_1;
        std::wstring description;
        if (!ReadPod(in, r.verdict) ||
            !ReadPod(in, r.riskScore) ||
            !ReadPod(in, r.matchedRules) ||
            !ReadPod(in, r.win32Error) ||
            !ReadPod(in, r.fileSize) ||
            !ReadPod(in, r.lastWriteTime) ||
            !ReadPod(in, r.entropy) ||
            !ReadPod(in, r.scanDurationMs) ||
            !ReadWide(in, description) ||
            !ReadPod(in, r.isPe) ||
            !ReadPod(in, r.isPe32Plus) ||
            !ReadPod(in, r.machine) ||
            !ReadPod(in, r.subsystem) ||
            !ReadPod(in, r.isDll) ||
            !ReadPod(in, r.isDriver) ||
            !ReadPod(in, r.isManaged) ||
            !ReadPod(in, r.isSigned) ||
            !ReadPod(in, r.signatureStatus) ||
            !ReadPod(in, r.hasDebug) ||
            !ReadPod(in, r.hasRichHeader) ||
            !ReadPod(in, r.entryPointRva) ||
            !ReadPod(in, r.imageBase) ||
            !ReadPod(in, r.sectionCount) ||
            !ReadPod(in, r.overlaySize))
        {
            return false;
        }
        wcsncpy_s(r.description, _countof(r.description), description.c_str(), _TRUNCATE);
        return true;
    }
}

// Tron hash cua path + file identity + engine/rule/schema version de dung trong unordered_map.
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
ResultCache::~ResultCache() { Flush(); }

// Cau hinh L2 cache file va cac version tham gia invalidation, sau do thu load du lieu cu.
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

    std::error_code ec;
    const std::filesystem::path path(filePath);
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
    return LoadLocked();
}

// CACHE LOOKUP: tim L1 memory cache va kiem tra TTL.
// Tra true = HIT; false = MISS, WorkerLoop se phai goi Engine.
bool ResultCache::TryGet(const CacheKey& key, EngineScanResultV1& result)
{
    std::lock_guard lock(mutex_);
    const auto it = entries_.find(key);
    if (it == entries_.end()) return false;
    if (std::chrono::system_clock::now() >= it->second.expiresAt)
    {
        entries_.erase(it);
        SaveLocked();
        return false;
    }
    result = it->second.result;
    return true;
}

// Ghi ket qua scan thanh cong vao L1 cache va danh dau dirty de sau do flush xuong L2 file.
void ResultCache::Put(const CacheKey& key, const EngineScanResultV1& result)
{
    std::lock_guard lock(mutex_);
    entries_[key] = Entry{result, std::chrono::system_clock::now() + ttl_};
    SaveLocked();
}

// Xoa entry het TTL de cache khong giu ket qua cu vo han.
void ResultCache::Cleanup()
{
    std::lock_guard lock(mutex_);
    const auto now = std::chrono::system_clock::now();
    bool changed = false;
    for (auto it = entries_.begin(); it != entries_.end();)
    {
        if (now >= it->second.expiresAt)
        {
            it = entries_.erase(it);
            changed = true;
        }
        else ++it;
    }
    if (changed) SaveLocked();
}

// Neu cache da thay doi, serialize L1 hien tai ra persistent cache file (L2).
void ResultCache::Flush()
{
    std::lock_guard lock(mutex_);
    SaveLocked();
}

// Doc persistent cache khi service start, dong thoi kiem tra magic/schema/engine/rule version.
// Version khong khop => bo cache cu de tranh dung ket qua cua engine/rule da thay doi.
bool ResultCache::LoadLocked()
{
    if (persistentPath_.empty()) return true;
    std::ifstream in(std::filesystem::path(persistentPath_), std::ios::binary);
    if (!in) return true; // First run: no L2 file yet.

    std::uint32_t magic = 0, format = 0, count = 0;
    if (!ReadPod(in, magic) || !ReadPod(in, format) || !ReadPod(in, count) ||
        magic != CACHE_MAGIC || format != FILE_FORMAT_VERSION || count > MAX_RECORDS)
    {
        return false;
    }

    const auto now = std::chrono::system_clock::now();
    for (std::uint32_t i = 0; i < count; ++i)
    {
        CacheKey key{};
        std::uint64_t expiresMs = 0;
        EngineScanResultV1 result{};
        if (!ReadWide(in, key.normalizedPath) ||
            !ReadPod(in, key.lastWriteTime) ||
            !ReadPod(in, key.fileSize) ||
            !ReadString(in, key.engineVersion) ||
            !ReadPod(in, key.ruleSetVersion) ||
            !ReadPod(in, key.cacheSchemaVersion) ||
            !ReadPod(in, expiresMs) ||
            !ReadResult(in, result))
        {
            entries_.clear();
            return false;
        }

        const auto expiresAt = FromUnixMs(expiresMs);
        if (expiresAt <= now) continue;
        if (key.engineVersion != engineVersion_ ||
            key.ruleSetVersion != ruleSetVersion_ ||
            key.cacheSchemaVersion != cacheSchemaVersion_)
        {
            continue; // Engine/rules/schema update => natural cache MISS.
        }
        entries_[std::move(key)] = Entry{result, expiresAt};
    }
    return true;
}

// Ghi cache qua file tam roi replace file chinh de giam nguy co file cache dang ghi bi hong.
bool ResultCache::SaveLocked()
{
    if (persistentPath_.empty()) return true;
    const std::filesystem::path target(persistentPath_);
    const std::filesystem::path temp = target.wstring() + L".tmp";
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    if (!out) return false;

    const std::uint32_t count = static_cast<std::uint32_t>((std::min<std::size_t>)(entries_.size(), MAX_RECORDS));
    WritePod(out, CACHE_MAGIC);
    WritePod(out, FILE_FORMAT_VERSION);
    WritePod(out, count);

    std::uint32_t written = 0;
    for (const auto& [key, entry] : entries_)
    {
        if (written++ >= count) break;
        WriteWide(out, key.normalizedPath);
        WritePod(out, key.lastWriteTime);
        WritePod(out, key.fileSize);
        WriteString(out, key.engineVersion);
        WritePod(out, key.ruleSetVersion);
        WritePod(out, key.cacheSchemaVersion);
        const std::uint64_t expiresMs = ToUnixMs(entry.expiresAt);
        WritePod(out, expiresMs);
        WriteResult(out, entry.result);
    }
    out.flush();
    if (!out) return false;
    out.close();

    std::error_code ec;
    std::filesystem::remove(target, ec);
    ec.clear();
    std::filesystem::rename(temp, target, ec);
    return !ec;
}
