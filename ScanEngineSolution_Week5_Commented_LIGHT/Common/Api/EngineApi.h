// ============================================================================
// MODULE : Common / API
// ROLE   : Contract shared by ScanService and ScanEngine.dll.
// WEEK 5 : PE metadata + MALFORMED_PE/STRUCT_CORRUPT + EngineInspectPe().
// ============================================================================

#pragma once

#include <Windows.h>
#include <cstdint>

#define ENGINE_API_VERSION_1 0x00010000u
#define ENGINE_VERSION_STRING "2.0.0-week5"

#if defined(SCANENGINE_USE_DEF)
#define SCANENGINE_API extern "C"
#elif defined(SCANENGINE_EXPORTS)
#define SCANENGINE_API extern "C" __declspec(dllexport)
#else
#define SCANENGINE_API extern "C"
#endif

// Cac ma trang thai on dinh tai bien DLL. Service dung enum nay thay vi phu thuoc truc tiep Win32 error.
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
    InternalError,
    MalformedPe,
    StructCorrupt
};

enum class EngineEventType : std::uint32_t
{
    Progress = 0,
    Result,
    Error,
    Cancelled
};

// Cac moc progress cua mot lan scan. EngineCallback stream stage nay ve Service/Client.
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
    Failed,
    ParsingPe,
    AnalyzingPe
};

enum class EngineVerdict : std::uint32_t
{
    Safe = 0,
    Suspicious,
    Malicious
};

enum class EngineSignatureStatus : std::uint32_t
{
    Unsigned = 0,
    SignedValid = 1,
    SignedInvalid = 2
};

// Bitmask rule: moi bit dai dien mot dau hieu/rule da match; nhieu rule co the OR voi nhau.
enum EngineRuleFlags : std::uint32_t
{
    ENGINE_RULE_NONE = 0x00000000,
    ENGINE_RULE_OUTSIDE_C_DRIVE = 0x00000001,
    ENGINE_RULE_RISKY_EXTENSION = 0x00000002,
    ENGINE_RULE_LARGE_FILE = 0x00000004,
    ENGINE_RULE_HIGH_ENTROPY = 0x00000008,

    ENGINE_RULE_PE_TIMESTAMP = 0x00000010,
    ENGINE_RULE_PE_ENTRYPOINT = 0x00000020,
    ENGINE_RULE_PE_ALIGNMENT = 0x00000040,
    ENGINE_RULE_PE_DIRECTORY = 0x00000080,
    ENGINE_RULE_PE_SECTION_NAME = 0x00000100,
    ENGINE_RULE_PE_WX_SECTION = 0x00000200,
    ENGINE_RULE_PE_EXEC_ENTROPY = 0x00000400,
    ENGINE_RULE_PE_OVERLAY = 0x00000800,
    ENGINE_RULE_PE_SECTION_COUNT = 0x00001000,
    ENGINE_RULE_PE_RISK_PROCESS = 0x00002000,
    ENGINE_RULE_PE_RISK_PERSISTENCE = 0x00004000,
    ENGINE_RULE_PE_RISK_NETWORK = 0x00008000,
    ENGINE_RULE_PE_RISK_CRYPTO = 0x00010000,
    ENGINE_RULE_PE_TLS_CALLBACK = 0x00020000,
    ENGINE_RULE_PE_DELAY_RISK = 0x00040000,
    ENGINE_RULE_PE_EXPORT_ANOMALY = 0x00080000,
    ENGINE_RULE_PE_VERSION_INFO = 0x00100000,
    ENGINE_RULE_PE_RESOURCE = 0x00200000,
    ENGINE_RULE_PE_SIGNATURE_INVALID = 0x00400000,
    ENGINE_RULE_PE_UNSIGNED = 0x00800000
};

// Options do Service truyen vao DLL cho tung Job. structSize + apiVersion dung de version hoa ABI.
struct EngineScanOptionsV1
{
    std::uint32_t structSize;
    std::uint32_t apiVersion;
    std::uint32_t timeoutMs;
    double entropyThreshold;
    std::uint64_t maxEntropyBytes;
    std::uint32_t progressIntervalMs;
    std::uint32_t reserved;
};

// Fields are appended to preserve the original Week-4 prefix layout.
// Ket qua scan day du tra tu DLL: verdict/risk/rules + metadata file + thong tin PE Week 5.
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

    // Week 5 PE inspection output.
    BOOL isPe;
    BOOL isPe32Plus;
    std::uint16_t machine;
    std::uint16_t subsystem;
    BOOL isDll;
    BOOL isDriver;
    BOOL isManaged;
    BOOL isSigned;
    std::uint32_t signatureStatus;
    BOOL hasDebug;
    BOOL hasRichHeader;
    std::uint32_t entryPointRva;
    std::uint64_t imageBase;
    std::uint16_t sectionCount;
    std::uint16_t reservedPe;
    std::uint64_t overlaySize;
};

struct EnginePeInfoV1
{
    std::uint32_t structSize;
    std::uint32_t apiVersion;
    BOOL isPe;
    BOOL isPe32Plus;
    std::uint16_t machine;
    std::uint16_t subsystem;
    BOOL isDll;
    BOOL isDriver;
    BOOL isManaged;
    BOOL isSigned;
    std::uint32_t signatureStatus;
    BOOL hasDebug;
    BOOL hasRichHeader;
    std::uint32_t entryPointRva;
    std::uint64_t imageBase;
    std::uint16_t sectionCount;
    std::uint16_t reserved;
    std::uint64_t overlaySize;
    std::uint32_t riskScore;
    std::uint32_t matchedRules;
};

// Goi du lieu callback. result chi co y nghia o event Result; message mo ta stage/loi hien tai.
struct EngineProgressInfoV1
{
    std::uint32_t structSize;
    std::uint32_t apiVersion;
    EngineEventType eventType;
    EngineScanStage stage;
    std::uint32_t progressPercent;
    EngineStatus status;
    const EngineScanResultV1* result;
    const wchar_t* message;
};

// Callback tra FALSE = ben Service yeu cau dung scan (co che cooperative cancellation).
using EngineProgressCallback = BOOL(WINAPI*)(
    const EngineProgressInfoV1* progressInfo,
    void* userContext);

SCANENGINE_API EngineStatus WINAPI EngineInitialize(const char* configJsonUtf8);
SCANENGINE_API EngineStatus WINAPI EngineScanFile(
    const wchar_t* path,
    const EngineScanOptionsV1* options,
    EngineProgressCallback callback,
    void* userContext);
SCANENGINE_API EngineStatus WINAPI EngineInspectPe(
    const wchar_t* path,
    EnginePeInfoV1* info);
SCANENGINE_API const char* WINAPI EngineGetVersion();
SCANENGINE_API void WINAPI EngineShutdown();

// Cac PFN_... la kieu function pointer de EngineLoader resolve export bang GetProcAddress.
using PFN_EngineInitialize = EngineStatus(WINAPI*)(const char* configJsonUtf8);
using PFN_EngineScanFile = EngineStatus(WINAPI*)(
    const wchar_t* path,
    const EngineScanOptionsV1* options,
    EngineProgressCallback callback,
    void* userContext);
using PFN_EngineInspectPe = EngineStatus(WINAPI*)(const wchar_t*, EnginePeInfoV1*);
using PFN_EngineGetVersion = const char*(WINAPI*)();
using PFN_EngineShutdown = void(WINAPI*)();
