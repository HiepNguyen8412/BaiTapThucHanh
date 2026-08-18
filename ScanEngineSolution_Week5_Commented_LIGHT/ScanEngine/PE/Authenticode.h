#pragma once

#include "Api/EngineApi.h"
#include <string>

namespace ScanEngineInternal
{
    EngineSignatureStatus VerifyAuthenticode(const std::wstring& path, bool hasSecurityDirectory);
}
