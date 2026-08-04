#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include "../Common/EngineApi.h"

#include <iostream>
#include <string>

namespace
{
    const wchar_t* StageText(EngineScanStage stage)
    {
        switch (stage)
        {
        case EngineScanStage::Starting: return L"Starting";
        case EngineScanStage::OpeningFile: return L"OpeningFile";
        case EngineScanStage::ReadingMetadata: return L"ReadingMetadata";
        case EngineScanStage::ReadingContent: return L"ReadingContent";
        case EngineScanStage::CalculatingEntropy: return L"CalculatingEntropy";
        case EngineScanStage::ApplyingRules: return L"ApplyingRules";
        case EngineScanStage::BuildingResult: return L"BuildingResult";
        case EngineScanStage::Completed: return L"Completed";
        case EngineScanStage::Cancelled: return L"Cancelled";
        case EngineScanStage::Failed: return L"Failed";
        default: return L"Unknown";
        }
    }

    BOOL WINAPI Callback(const EngineProgressInfoV1* info, void*)
    {
        if (info == nullptr) return FALSE;
        std::wcout << L'[' << info->progressPercent << L"%] "
            << StageText(info->stage);
        if (info->message != nullptr) std::wcout << L" - " << info->message;
        std::wcout << L'\n';
        if (info->result != nullptr)
        {
            std::wcout << L"score=" << info->result->riskScore
                << L", rules=0x" << std::hex << info->result->matchedRules << std::dec
                << L", entropy=" << info->result->entropy
                << L", size=" << info->result->fileSize << L'\n';
        }
        return TRUE;
    }
}

int wmain(int argc, wchar_t* argv[])
{
    if (argc < 2)
    {
        std::wcout << L"Usage: ScanEngineTest.exe <file-path>\n";
        return 1;
    }

    HMODULE module = LoadLibraryW(L"ScanEngine.dll");
    if (module == nullptr)
    {
        std::wcerr << L"LoadLibrary failed. Error=" << GetLastError() << L'\n';
        return 1;
    }

    auto initialize = reinterpret_cast<PFN_EngineInitialize>(GetProcAddress(module, "EngineInitialize"));
    auto scan = reinterpret_cast<PFN_EngineScanFile>(GetProcAddress(module, "EngineScanFile"));
    auto version = reinterpret_cast<PFN_EngineGetVersion>(GetProcAddress(module, "EngineGetVersion"));
    auto shutdown = reinterpret_cast<PFN_EngineShutdown>(GetProcAddress(module, "EngineShutdown"));
    if (initialize == nullptr || scan == nullptr || version == nullptr || shutdown == nullptr)
    {
        std::wcerr << L"Missing DLL export. Error=" << GetLastError() << L'\n';
        FreeLibrary(module);
        return 1;
    }

    std::cout << "Engine version: " << version() << '\n';
    const auto initStatus = initialize(nullptr);
    if (initStatus != EngineStatus::Success && initStatus != EngineStatus::AlreadyInitialized)
    {
        std::wcerr << L"EngineInitialize failed.\n";
        FreeLibrary(module);
        return 1;
    }

    EngineScanOptionsV1 options{};
    options.structSize = sizeof(options);
    options.apiVersion = ENGINE_API_VERSION_1;
    options.timeoutMs = 30000;
    options.entropyThreshold = 7.2;
    options.maxEntropyBytes = 1024ull * 1024ull;
    options.progressIntervalMs = 100;

    const auto status = scan(argv[1], &options, Callback, nullptr);
    std::wcout << L"EngineScanFile status=" << static_cast<std::uint32_t>(status) << L'\n';
    shutdown();
    FreeLibrary(module);
    return status == EngineStatus::Success ? 0 : 2;
}
