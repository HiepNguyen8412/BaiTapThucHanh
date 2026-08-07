// ============================================================================
// MODULE : Common / API
// ROLE   : Hop dong chung giua ScanService va ScanEngine.dll (status, options, callback, result).
// NOTE   : File duoc sap xep lai theo kien truc module de de doc va thuyet trinh.
// ============================================================================

#pragma once

#include <Windows.h>
#include <cstdint>

#define ENGINE_API_VERSION_1 0x00010000u
#define ENGINE_VERSION_STRING "1.0.0"

#if defined(SCANENGINE_USE_DEF)
#define SCANENGINE_API extern "C"
#elif defined(SCANENGINE_EXPORTS)
#define SCANENGINE_API extern "C" __declspec(dllexport)
#else
// Consumers load the DLL dynamically with LoadLibrary/GetProcAddress.
#define SCANENGINE_API extern "C"
#endif

enum class EngineStatus : std::uint32_t
{
    Success = 0,
    InvalidArgument,
    InvalidApiVersion,
    InvalidStructureSize,
    NotInitialized,
    AlreadyInitialized,
    FileNotFound,
    AccessDenied,
    OpenFileFailed,
    ReadFileFailed,
    Timeout,
    Cancelled,
    InternalError
};

enum class EngineEventType : std::uint32_t
{
    Progress = 0,
    Result,
    Error,
    Cancelled
};

enum class EngineScanStage : std::uint32_t
{
    Starting = 0,
    OpeningFile,
    ReadingMetadata,
    ReadingContent,
    CalculatingEntropy,
    ApplyingRules,
    BuildingResult,
    Completed,
    Cancelled,
    Failed
};

enum class EngineVerdict : std::uint32_t
{
    Safe = 0,
    Suspicious,
    Malicious
};

enum EngineRuleFlags : std::uint32_t
{
    ENGINE_RULE_NONE = 0x00000000,
    ENGINE_RULE_OUTSIDE_C_DRIVE = 0x00000001,
    ENGINE_RULE_RISKY_EXTENSION = 0x00000002,
    ENGINE_RULE_LARGE_FILE = 0x00000004,
    ENGINE_RULE_HIGH_ENTROPY = 0x00000008
};

struct EngineScanOptionsV1
{
    std::uint32_t structSize;
    std::uint32_t apiVersion;
    std::uint32_t timeoutMs;          // 0 = no timeout
    double entropyThreshold;          // Recommended demo value: 7.2
    std::uint64_t maxEntropyBytes;    // Recommended demo value: 1 MiB
    std::uint32_t progressIntervalMs; // 0 = report every read chunk
    std::uint32_t reserved;
};

struct EngineScanResultV1
{
    std::uint32_t structSize;
    std::uint32_t apiVersion;
    EngineVerdict verdict;
    std::uint32_t riskScore;
    std::uint32_t matchedRules;
    std::uint32_t win32Error;
    std::uint64_t fileSize;
    std::uint64_t lastWriteTime;
    double entropy;
    std::uint64_t scanDurationMs;
    wchar_t description[256];
};

struct EngineProgressInfoV1
{
    std::uint32_t structSize;
    std::uint32_t apiVersion;
    EngineEventType eventType;
    EngineScanStage stage;
    std::uint32_t progressPercent;
    EngineStatus status;
    const EngineScanResultV1* result; // Valid only during callback.
    const wchar_t* message;           // Valid only during callback.
};

using EngineProgressCallback = BOOL(WINAPI*)(
    const EngineProgressInfoV1* progressInfo,
    void* userContext);

SCANENGINE_API EngineStatus WINAPI EngineInitialize(const char* configJsonUtf8);
SCANENGINE_API EngineStatus WINAPI EngineScanFile(
    const wchar_t* path,
    const EngineScanOptionsV1* options,
    EngineProgressCallback callback,
    void* userContext);
SCANENGINE_API const char* WINAPI EngineGetVersion();
SCANENGINE_API void WINAPI EngineShutdown();

using PFN_EngineInitialize = EngineStatus(WINAPI*)(const char* configJsonUtf8);
using PFN_EngineScanFile = EngineStatus(WINAPI*)(
    const wchar_t* path,
    const EngineScanOptionsV1* options,
    EngineProgressCallback callback,
    void* userContext);
using PFN_EngineGetVersion = const char*(WINAPI*)();
using PFN_EngineShutdown = void(WINAPI*)();
