// ============================================================================
// MODULE : ScanEngine / DLL Entry
// ROLE   : DLL lifecycle toi thieu; tat thread attach notifications.

// ============================================================================

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

// Entry point cua Windows DLL. Giu DllMain nhe, khong khoi tao scan/thread o day
// de tranh loader-lock; khoi tao that su duoc dat trong EngineInitialize.
BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved)
{
    UNREFERENCED_PARAMETER(reserved);
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}
