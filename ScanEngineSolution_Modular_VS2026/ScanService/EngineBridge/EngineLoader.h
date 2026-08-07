// ============================================================================
// MODULE : ScanService / EngineBridge
// ROLE   : Cau noi dong toi ScanEngine.dll bang LoadLibrary/GetProcAddress.
// NOTE   : File duoc sap xep lai theo kien truc module de de doc va thuyet trinh.
// ============================================================================

#pragma once

#include "Api/EngineApi.h"

#include <Windows.h>
#include <string>

class Logger;

class EngineLoader
{
public:
    explicit EngineLoader(Logger& logger);
    ~EngineLoader();

    bool Load(const std::wstring& dllPath);
    void Unload();
    bool IsLoaded() const noexcept { return module_ != nullptr; }
    EngineStatus Scan(
        const wchar_t* path,
        const EngineScanOptionsV1* options,
        EngineProgressCallback callback,
        void* context) const;
    std::string Version() const;

private:
    Logger& logger_;
    HMODULE module_{nullptr};
    PFN_EngineInitialize initialize_{nullptr};
    PFN_EngineScanFile scanFile_{nullptr};
    PFN_EngineGetVersion getVersion_{nullptr};
    PFN_EngineShutdown shutdown_{nullptr};
};
