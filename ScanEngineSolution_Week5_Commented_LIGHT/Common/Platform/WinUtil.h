// ============================================================================
// MODULE : Common / Platform
// ROLE   : Tien ich Windows dung chung: path, file identity, error text, user.

// ============================================================================

#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstdint>
#include <string>

namespace WinUtil
{
    std::wstring GetLastErrorMessage(DWORD errorCode);
    std::wstring GetCurrentUserNameString();
    std::wstring GetExecutableDirectory();
    std::wstring JoinPath(const std::wstring& left, const std::wstring& right);
    std::wstring NormalizePath(const std::wstring& path, DWORD& errorCode);
    bool GetFileIdentity(
        const std::wstring& path,
        std::uint64_t& fileSize,
        std::uint64_t& lastWriteTime,
        DWORD& errorCode);
}
