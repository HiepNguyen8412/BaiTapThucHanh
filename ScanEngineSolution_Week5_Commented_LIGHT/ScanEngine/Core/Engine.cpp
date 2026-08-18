// ============================================================================
// MODULE : ScanEngine / Core
// ROLE   : Public Engine API; orchestration scan, rule scoring, result callback.

// ============================================================================

#include "Api/EngineApi.h"
#include "Analysis/FileAnalyzer.h"
#include "PE/PeReader.h"
#include "PE/PeAnalyzer.h"
#include "PE/PeRuleEvaluator.h"

#include <Windows.h>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <mutex>
#include <string>

namespace
{
    std::atomic_bool g_initialized{false};
    constexpr std::uint64_t LARGE_FILE_THRESHOLD = 50ull * 1024ull * 1024ull;

    struct EngineConfig
    {
        double entropyThreshold{7.2};
        std::uint64_t maxEntropyBytes{1024ull * 1024ull};
        std::uint32_t progressIntervalMs{100};
    };

    EngineConfig g_config{};
    std::mutex g_configMutex;

    // Parser config toi gian: tim numeric value theo key trong JSON config luc EngineInitialize.
    bool TryReadJsonNumber(const char* json, const char* key, double& value)
    {
        if (json == nullptr || key == nullptr) return false;
        const std::string quotedKey = std::string("\"") + key + "\"";
        const char* keyPosition = std::strstr(json, quotedKey.c_str());
        if (keyPosition == nullptr) return false;
        const char* colon = std::strchr(keyPosition + quotedKey.size(), ':');
        if (colon == nullptr) return false;
        char* end = nullptr;
        const double parsed = std::strtod(colon + 1, &end);
        if (end == colon + 1) return false;
        value = parsed;
        return true;
    }

    // Dong goi EngineProgressInfoV1 va goi callback ve Service.
    // Gia tri callback FALSE la co che hop tac de Service yeu cau engine CANCEL.
    bool ReportEvent(
        EngineProgressCallback callback,
        void* userContext,
        EngineEventType eventType,
        EngineScanStage stage,
        std::uint32_t progress,
        EngineStatus status,
        const wchar_t* message,
        const EngineScanResultV1* result = nullptr)
    {
        if (callback == nullptr)
        {
            return true;
        }
        EngineProgressInfoV1 info{};
        info.structSize = sizeof(info);
        info.apiVersion = ENGINE_API_VERSION_1;
        info.eventType = eventType;
        info.stage = stage;
        info.progressPercent = progress > 100 ? 100 : progress;
        info.status = status;
        info.result = result;
        info.message = message;
        return callback(&info, userContext) != FALSE;
    }

    // Gui mot Error/Cancelled event cuoi cung truoc khi EngineScanFile tra EngineStatus loi.
    EngineStatus FinishWithError(
        EngineProgressCallback callback,
        void* userContext,
        EngineStatus status,
        const wchar_t* message)
    {
        const EngineEventType eventType = status == EngineStatus::Cancelled
            ? EngineEventType::Cancelled
            : EngineEventType::Error;
        const EngineScanStage stage = status == EngineStatus::Cancelled
            ? EngineScanStage::Cancelled
            : EngineScanStage::Failed;
        ReportEvent(callback, userContext, eventType, stage, 100, status, message);
        return status;
    }

    // Chuyen tong risk score thanh SAFE/SUSPICIOUS/MALICIOUS theo nguong cua demo.
    EngineVerdict MapVerdict(std::uint32_t score)
    {
        // Week 5 has many more heuristics than Week 4.
        if (score <= 2) return EngineVerdict::Safe;
        if (score <= 6) return EngineVerdict::Suspicious;
        return EngineVerdict::Malicious;
    }

    EngineStatus MapPeStatus(ScanEngineInternal::PeParseStatus status)
    {
        switch (status)
        {
        case ScanEngineInternal::PeParseStatus::MalformedPe: return EngineStatus::MalformedPe;
        case ScanEngineInternal::PeParseStatus::StructCorrupt: return EngineStatus::StructCorrupt;
        case ScanEngineInternal::PeParseStatus::IoError: return EngineStatus::ReadFileFailed;
        default: return EngineStatus::Success;
        }
    }
}

// API KHOI TAO DLL: validate/cap nhat config va dat g_initialized.
// Service phai goi thanh cong ham nay truoc moi EngineScanFile/EngineInspectPe.
SCANENGINE_API EngineStatus WINAPI EngineInitialize(const char* configJsonUtf8)
{
    bool expected = false;
    if (!g_initialized.compare_exchange_strong(expected, true))
    {
        return EngineStatus::AlreadyInitialized;
    }

    EngineConfig config{};
    if (configJsonUtf8 != nullptr && configJsonUtf8[0] != '\0')
    {
        double number = 0.0;
        if (TryReadJsonNumber(configJsonUtf8, "entropyThreshold", number))
        {
            if (number <= 0.0 || number > 8.0)
            {
                g_initialized.store(false);
                return EngineStatus::InvalidArgument;
            }
            config.entropyThreshold = number;
        }
        if (TryReadJsonNumber(configJsonUtf8, "maxEntropyBytes", number))
        {
            if (number < 1.0)
            {
                g_initialized.store(false);
                return EngineStatus::InvalidArgument;
            }
            config.maxEntropyBytes = static_cast<std::uint64_t>(number);
        }
        if (TryReadJsonNumber(configJsonUtf8, "progressIntervalMs", number))
        {
            if (number < 0.0 || number > 60000.0)
            {
                g_initialized.store(false);
                return EngineStatus::InvalidArgument;
            }
            config.progressIntervalMs = static_cast<std::uint32_t>(number);
        }
    }

    {
        std::lock_guard lock(g_configMutex);
        g_config = config;
    }
    return EngineStatus::Success;
}

// Tra version chuoi co dinh cua Engine; Service dua no vao cache key.
SCANENGINE_API const char* WINAPI EngineGetVersion()
{
    return ENGINE_VERSION_STRING;
}

// MAIN ENGINE FLOW:
// validate -> normalize -> metadata -> entropy -> rules -> result -> callback 100%.
// Tat ca chay tren worker thread cua Service; DLL khong tao worker rieng.
// LUONG SCAN CHINH TRONG DLL:
// validate -> normalize path -> metadata -> entropy -> parse/analyze PE -> apply rules -> build result.
// Moi stage goi ReportEvent de streaming progress ve Service/Client.
SCANENGINE_API EngineStatus WINAPI EngineScanFile(
    const wchar_t* path,
    const EngineScanOptionsV1* options,
    EngineProgressCallback callback,
    void* userContext)
{
    if (!g_initialized.load())
    {
        return EngineStatus::NotInitialized;
    }
    if (path == nullptr || path[0] == L'\0' || options == nullptr)
    {
        return EngineStatus::InvalidArgument;
    }
    if (options->structSize != sizeof(EngineScanOptionsV1))
    {
        return EngineStatus::InvalidStructureSize;
    }
    if (options->apiVersion != ENGINE_API_VERSION_1)
    {
        return EngineStatus::InvalidApiVersion;
    }
    EngineConfig config{};
    {
        std::lock_guard lock(g_configMutex);
        config = g_config;
    }
    const double entropyThreshold = options->entropyThreshold > 0.0
        ? options->entropyThreshold
        : config.entropyThreshold;
    const std::uint64_t maxEntropyBytes = options->maxEntropyBytes != 0
        ? options->maxEntropyBytes
        : config.maxEntropyBytes;
    const std::uint32_t progressIntervalMs = options->progressIntervalMs != 0
        ? options->progressIntervalMs
        : config.progressIntervalMs;
    if (maxEntropyBytes == 0 || entropyThreshold <= 0.0 || entropyThreshold > 8.0)
    {
        return EngineStatus::InvalidArgument;
    }

    const ULONGLONG startTick = GetTickCount64();
    if (!ReportEvent(
            callback,
            userContext,
            EngineEventType::Progress,
            EngineScanStage::Starting,
            0,
            EngineStatus::Success,
            L"Starting scan"))
    {
        return FinishWithError(
            callback, userContext, EngineStatus::Cancelled, L"Scan cancelled");
    }

    DWORD win32Error = ERROR_SUCCESS;
    std::wstring normalizedPath;
    EngineStatus status = ScanEngineInternal::NormalizeFilePath(
        path, normalizedPath, win32Error);
    if (status != EngineStatus::Success)
    {
        return FinishWithError(callback, userContext, status, L"Invalid file path");
    }

    if (!ReportEvent(
            callback,
            userContext,
            EngineEventType::Progress,
            EngineScanStage::OpeningFile,
            5,
            EngineStatus::Success,
            L"Opening file"))
    {
        return FinishWithError(
            callback, userContext, EngineStatus::Cancelled, L"Scan cancelled");
    }

    ScanEngineInternal::FileMetadata metadata{};
    status = ScanEngineInternal::ReadFileMetadata(
        normalizedPath, metadata, win32Error);
    if (status != EngineStatus::Success)
    {
        return FinishWithError(callback, userContext, status, L"Cannot read file metadata");
    }

    if (!ReportEvent(
            callback,
            userContext,
            EngineEventType::Progress,
            EngineScanStage::ReadingMetadata,
            15,
            EngineStatus::Success,
            L"File metadata read"))
    {
        return FinishWithError(
            callback, userContext, EngineStatus::Cancelled, L"Scan cancelled");
    }

    double entropy = 0.0;
    status = ScanEngineInternal::CalculateFileEntropy(
        normalizedPath,
        maxEntropyBytes,
        entropy,
        win32Error,
        callback,
        userContext,
        20,
        75,
        progressIntervalMs,
        startTick,
        options->timeoutMs);
    if (status != EngineStatus::Success)
    {
        const wchar_t* message = status == EngineStatus::Timeout
            ? L"Scan timeout"
            : status == EngineStatus::Cancelled
                ? L"Scan cancelled"
                : L"Cannot read file content";
        return FinishWithError(callback, userContext, status, message);
    }

    if (!ReportEvent(
            callback,
            userContext,
            EngineEventType::Progress,
            EngineScanStage::ParsingPe,
            78,
            EngineStatus::Success,
            L"Parsing PE headers"))
    {
        return FinishWithError(
            callback, userContext, EngineStatus::Cancelled, L"Scan cancelled");
    }

    ScanEngineInternal::PeReader peReader;
    ScanEngineInternal::PeAnalysis peAnalysis{};
    const auto peParseStatus = peReader.Open(normalizedPath, win32Error);
    if (peParseStatus == ScanEngineInternal::PeParseStatus::MalformedPe ||
        peParseStatus == ScanEngineInternal::PeParseStatus::StructCorrupt ||
        peParseStatus == ScanEngineInternal::PeParseStatus::IoError)
    {
        const EngineStatus peError = MapPeStatus(peParseStatus);
        const wchar_t* peMessage = peError == EngineStatus::MalformedPe
            ? L"MALFORMED_PE: truncated or invalid PE header"
            : peError == EngineStatus::StructCorrupt
                ? L"STRUCT_CORRUPT: PE section/directory range is invalid"
                : L"Cannot read file while parsing PE";
        return FinishWithError(callback, userContext, peError, peMessage);
    }

    if (peParseStatus == ScanEngineInternal::PeParseStatus::Success)
    {
        if (!ReportEvent(
                callback,
                userContext,
                EngineEventType::Progress,
                EngineScanStage::AnalyzingPe,
                82,
                EngineStatus::Success,
                L"Analyzing PE sections/imports/TLS/resources/signature"))
        {
            return FinishWithError(
                callback, userContext, EngineStatus::Cancelled, L"Scan cancelled");
        }
        ScanEngineInternal::PeAnalyzer::Analyze(normalizedPath, peReader, 7.4, peAnalysis);
        ScanEngineInternal::PeRuleEvaluator::Evaluate(peReader, peAnalysis, false);
    }

    if (!ReportEvent(
            callback,
            userContext,
            EngineEventType::Progress,
            EngineScanStage::ApplyingRules,
            88,
            EngineStatus::Success,
            L"Applying detection rules"))
    {
        return FinishWithError(
            callback, userContext, EngineStatus::Cancelled, L"Scan cancelled");
    }

    std::uint32_t score = peAnalysis.score;
    std::uint32_t rules = peAnalysis.matchedRules;
    if (ScanEngineInternal::IsOutsideCDrive(normalizedPath))
    {
        score += 2;
        rules |= ENGINE_RULE_OUTSIDE_C_DRIVE;
    }
    if (ScanEngineInternal::HasRiskyExtension(normalizedPath))
    {
        score += 1;
        rules |= ENGINE_RULE_RISKY_EXTENSION;
    }
    if (metadata.fileSize > LARGE_FILE_THRESHOLD)
    {
        score += 1;
        rules |= ENGINE_RULE_LARGE_FILE;
    }
    if (entropy > entropyThreshold)
    {
        score += 1;
        rules |= ENGINE_RULE_HIGH_ENTROPY;
    }

    if (!ReportEvent(
            callback,
            userContext,
            EngineEventType::Progress,
            EngineScanStage::BuildingResult,
            95,
            EngineStatus::Success,
            L"Building result"))
    {
        return FinishWithError(
            callback, userContext, EngineStatus::Cancelled, L"Scan cancelled");
    }

    EngineScanResultV1 result{};
    result.structSize = sizeof(result);
    result.apiVersion = ENGINE_API_VERSION_1;
    result.verdict = MapVerdict(score);
    result.riskScore = score;
    result.matchedRules = rules;
    result.win32Error = win32Error;
    result.fileSize = metadata.fileSize;
    result.lastWriteTime = metadata.lastWriteTime;
    result.entropy = entropy;
    result.scanDurationMs = GetTickCount64() - startTick;

    if (peParseStatus == ScanEngineInternal::PeParseStatus::Success)
    {
        const auto& pe = peReader.Image();
        result.isPe = TRUE;
        result.isPe32Plus = pe.isPe32Plus ? TRUE : FALSE;
        result.machine = pe.machine;
        result.subsystem = pe.subsystem;
        result.isDll = pe.isDll ? TRUE : FALSE;
        result.isDriver = pe.isDriver ? TRUE : FALSE;
        result.isManaged = pe.isManaged ? TRUE : FALSE;
        result.isSigned = pe.isSigned ? TRUE : FALSE;
        result.signatureStatus = static_cast<std::uint32_t>(peAnalysis.signatureStatus);
        result.hasDebug = pe.hasDebug ? TRUE : FALSE;
        result.hasRichHeader = pe.hasRichHeader ? TRUE : FALSE;
        result.entryPointRva = pe.entryPointRva;
        result.imageBase = pe.imageBase;
        result.sectionCount = pe.sectionCount;
        result.overlaySize = pe.overlaySize;
    }

    const wchar_t* verdictText = result.verdict == EngineVerdict::Safe
        ? L"SAFE"
        : result.verdict == EngineVerdict::Suspicious
            ? L"SUSPICIOUS"
            : L"MALICIOUS";
    swprintf_s(
        result.description,
        _countof(result.description),
        L"Verdict=%ls, score=%u, rules=0x%08X, entropy=%.3f",
        verdictText,
        result.riskScore,
        result.matchedRules,
        result.entropy);

    if (!ReportEvent(
            callback,
            userContext,
            EngineEventType::Result,
            EngineScanStage::Completed,
            100,
            EngineStatus::Success,
            result.description,
            &result))
    {
        return EngineStatus::Cancelled;
    }
    return EngineStatus::Success;
}

// API rut gon chi parse/analyze PE va dien EnginePeInfoV1, khong chay toan bo luong scan rule chung.
SCANENGINE_API EngineStatus WINAPI EngineInspectPe(
    const wchar_t* path,
    EnginePeInfoV1* info)
{
    if (!g_initialized.load()) return EngineStatus::NotInitialized;
    if (path == nullptr || path[0] == L'\0' || info == nullptr) return EngineStatus::InvalidArgument;
    if (info->structSize != sizeof(EnginePeInfoV1)) return EngineStatus::InvalidStructureSize;
    if (info->apiVersion != ENGINE_API_VERSION_1) return EngineStatus::InvalidApiVersion;

    DWORD win32Error = ERROR_SUCCESS;
    std::wstring normalizedPath;
    EngineStatus status = ScanEngineInternal::NormalizeFilePath(path, normalizedPath, win32Error);
    if (status != EngineStatus::Success) return status;

    ScanEngineInternal::PeReader reader;
    const auto parse = reader.Open(normalizedPath, win32Error);
    if (parse == ScanEngineInternal::PeParseStatus::NotPe)
    {
        EnginePeInfoV1 empty{};
        empty.structSize = sizeof(empty);
        empty.apiVersion = ENGINE_API_VERSION_1;
        *info = empty;
        return EngineStatus::Success;
    }
    if (parse != ScanEngineInternal::PeParseStatus::Success) return MapPeStatus(parse);

    ScanEngineInternal::PeAnalysis analysis{};
    ScanEngineInternal::PeAnalyzer::Analyze(normalizedPath, reader, 7.4, analysis);
    ScanEngineInternal::PeRuleEvaluator::Evaluate(reader, analysis, false);
    ScanEngineInternal::PeRuleEvaluator::FillPeInfo(reader, analysis, *info);
    return EngineStatus::Success;
}

// Reset initialized/config. Service goi ham nay truoc FreeLibrary.
SCANENGINE_API void WINAPI EngineShutdown()
{
    g_initialized.store(false);
    std::lock_guard lock(g_configMutex);
    g_config = EngineConfig{};
}
