// ============================================================================
// MODULE : ScanEngine / DLL Entry
// ROLE   : DLL lifecycle toi thieu; tat thread attach notifications.
// NOTE   : File duoc sap xep lai theo kien truc module de de doc va thuyet trinh.
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
