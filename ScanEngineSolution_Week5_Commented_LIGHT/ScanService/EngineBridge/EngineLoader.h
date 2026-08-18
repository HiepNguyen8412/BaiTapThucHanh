// ============================================================================
// MODULE : ScanService / EngineBridge
// ROLE   : Cau noi dong toi ScanEngine.dll bang LoadLibrary/GetProcAddress.

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
    EngineStatus InspectPe(const wchar_t* path, EnginePeInfoV1* info) const;
    std::string Version() const;

private:
    Logger& logger_;
    HMODULE module_{nullptr};
    PFN_EngineInitialize initialize_{nullptr};
    PFN_EngineScanFile scanFile_{nullptr};
    PFN_EngineInspectPe inspectPe_{nullptr};
    PFN_EngineGetVersion getVersion_{nullptr};
    PFN_EngineShutdown shutdown_{nullptr};
};
