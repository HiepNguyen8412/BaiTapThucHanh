// ============================================================================
// MODULE : ScanService / EngineBridge
// ROLE   : Nap DLL, resolve function pointers, goi EngineScanFile, unload.
// NOTE   : File duoc sap xep lai theo kien truc module de de doc va thuyet trinh.
// ============================================================================

#include "EngineBridge/EngineLoader.h"
#include "Monitoring/Logger.h"
#include "Platform/WinUtil.h"

#include <sstream>

EngineLoader::EngineLoader(Logger& logger) : logger_(logger) {}
EngineLoader::~EngineLoader() { Unload(); }

// Load() chi goi khi Service start: nap DLL va cache 4 function pointer.
bool EngineLoader::Load(const std::wstring& dllPath)
{
    Unload();
    module_ = LoadLibraryW(dllPath.c_str());
    if (module_ == nullptr)
    {
        const DWORD error = GetLastError();
        logger_.Error(L"LoadLibrary failed: " + WinUtil::GetLastErrorMessage(error));
        return false;
    }

    initialize_ = reinterpret_cast<PFN_EngineInitialize>(
        GetProcAddress(module_, "EngineInitialize"));
    scanFile_ = reinterpret_cast<PFN_EngineScanFile>(
        GetProcAddress(module_, "EngineScanFile"));
    getVersion_ = reinterpret_cast<PFN_EngineGetVersion>(
        GetProcAddress(module_, "EngineGetVersion"));
    shutdown_ = reinterpret_cast<PFN_EngineShutdown>(
        GetProcAddress(module_, "EngineShutdown"));

    if (initialize_ == nullptr || scanFile_ == nullptr ||
        getVersion_ == nullptr || shutdown_ == nullptr)
    {
        logger_.Error(L"ScanEngine.dll is missing one or more required exports.");
        Unload();
        return false;
    }

    const EngineStatus status = initialize_(
        "{\"entropyThreshold\":7.2,\"maxEntropyBytes\":1048576,\"progressIntervalMs\":100}");
    if (status != EngineStatus::Success && status != EngineStatus::AlreadyInitialized)
    {
        std::wstringstream stream;
        stream << L"EngineInitialize failed. Status=" << static_cast<std::uint32_t>(status);
        logger_.Error(stream.str());
        Unload();
        return false;
    }

    const std::string version = Version();
    logger_.Info(L"Loaded ScanEngine.dll version " + std::wstring(version.begin(), version.end()));
    return true;
}

void EngineLoader::Unload()
{
    if (module_ != nullptr)
    {
        if (shutdown_ != nullptr) shutdown_();
        FreeLibrary(module_);
    }
    module_ = nullptr;
    initialize_ = nullptr;
    scanFile_ = nullptr;
    getVersion_ = nullptr;
    shutdown_ = nullptr;
}

// Scan() khong tao thread. Ham EngineScanFile chay tren CHINH worker thread goi ham nay.
EngineStatus EngineLoader::Scan(
    const wchar_t* path,
    const EngineScanOptionsV1* options,
    EngineProgressCallback callback,
    void* context) const
{
    if (scanFile_ == nullptr) return EngineStatus::NotInitialized;
    return scanFile_(path, options, callback, context);
}

std::string EngineLoader::Version() const
{
    if (getVersion_ == nullptr) return {};
    const char* value = getVersion_();
    return value == nullptr ? std::string{} : std::string(value);
}
