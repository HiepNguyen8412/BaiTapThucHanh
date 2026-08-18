// ============================================================================
// MODULE : ScanEngine / DLL Entry
// ROLE   : DLL lifecycle toi thieu; tat thread attach notifications.

// ============================================================================

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved)
{
    UNREFERENCED_PARAMETER(reserved);
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}
