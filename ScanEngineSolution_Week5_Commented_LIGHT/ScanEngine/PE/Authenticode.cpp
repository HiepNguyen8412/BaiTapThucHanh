#include "PE/Authenticode.h"

#include <Windows.h>
#include <Softpub.h>
#include <WinTrust.h>

#pragma comment(lib, "wintrust.lib")
#pragma comment(lib, "crypt32.lib")

namespace ScanEngineInternal
{
    // Goi WinVerifyTrust de kiem tra chu ky Authenticode cua file.
    // Co Security Directory khong dong nghia chu ky hop le, nen van phai verify chain/trust.
    EngineSignatureStatus VerifyAuthenticode(const std::wstring& path, bool hasSecurityDirectory)
    {
        if (!hasSecurityDirectory) return EngineSignatureStatus::Unsigned;

        WINTRUST_FILE_INFO fileInfo{};
        fileInfo.cbStruct = sizeof(fileInfo);
        fileInfo.pcwszFilePath = path.c_str();

        WINTRUST_DATA trustData{};
        trustData.cbStruct = sizeof(trustData);
        trustData.dwUIChoice = WTD_UI_NONE;
        trustData.fdwRevocationChecks = WTD_REVOKE_NONE;
        trustData.dwUnionChoice = WTD_CHOICE_FILE;
        trustData.pFile = &fileInfo;
        trustData.dwStateAction = WTD_STATEACTION_VERIFY;
        trustData.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;

        GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
        const LONG status = WinVerifyTrust(nullptr, &action, &trustData);

        trustData.dwStateAction = WTD_STATEACTION_CLOSE;
        WinVerifyTrust(nullptr, &action, &trustData);

        return status == ERROR_SUCCESS
            ? EngineSignatureStatus::SignedValid
            : EngineSignatureStatus::SignedInvalid;
    }
}
