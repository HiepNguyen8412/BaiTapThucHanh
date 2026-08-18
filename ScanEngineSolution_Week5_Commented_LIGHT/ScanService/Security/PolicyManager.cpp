#include "Security/PolicyManager.h"

#include <Windows.h>
#include <algorithm>
#include <cwctype>
#include <vector>

namespace
{
    std::wstring NormalizeCaseAndSlashes(std::wstring value)
    {
        std::replace(value.begin(), value.end(), L'/', L'\\');
        if (value.rfind(L"\\\\?\\", 0) == 0) value.erase(0, 4);
        std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch)
        {
            return static_cast<wchar_t>(std::towlower(ch));
        });
        while (value.size() > 3 && value.back() == L'\\') value.pop_back();
        return value;
    }

    bool IsUnder(const std::wstring& path, const std::wstring& root)
    {
        if (path == root) return true;
        if (path.size() <= root.size()) return false;
        if (path.compare(0, root.size(), root) != 0) return false;
        return path[root.size()] == L'\\';
    }

    // Chuan hoa path truoc khi kiem policy, tranh bypass bang .., slash khac nhau, path tuong doi.
    bool ResolvePath(const std::wstring& input, std::wstring& output)
    {
        output.clear();
        const DWORD required = GetFullPathNameW(input.c_str(), 0, nullptr, nullptr);
        if (required == 0) return false;
        std::vector<wchar_t> full(required + 1, L'\0');
        const DWORD actual = GetFullPathNameW(input.c_str(), static_cast<DWORD>(full.size()), full.data(), nullptr);
        if (actual == 0 || actual >= full.size()) return false;
        output.assign(full.data(), actual);

        HANDLE file = CreateFileW(
            output.c_str(),
            FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (file != INVALID_HANDLE_VALUE)
        {
            const DWORD finalRequired = GetFinalPathNameByHandleW(file, nullptr, 0, FILE_NAME_NORMALIZED);
            if (finalRequired != 0)
            {
                std::vector<wchar_t> finalPath(finalRequired + 1, L'\0');
                const DWORD finalActual = GetFinalPathNameByHandleW(
                    file, finalPath.data(), static_cast<DWORD>(finalPath.size()), FILE_NAME_NORMALIZED);
                if (finalActual != 0 && finalActual < finalPath.size())
                {
                    output.assign(finalPath.data(), finalActual);
                }
            }
            CloseHandle(file);
        }
        return true;
    }
}

// Khoi tao cac root/extension/quy tac policy mac dinh cua Service.
PolicyManager::PolicyManager()
{
    wchar_t windowsDir[MAX_PATH]{};
    const UINT length = GetWindowsDirectoryW(windowsDir, MAX_PATH);
    if (length != 0 && length < MAX_PATH)
    {
        deniedRoots_.push_back(NormalizeCaseAndSlashes(std::wstring(windowsDir) + L"\\System32"));
    }
    else
    {
        deniedRoots_.push_back(L"c:\\windows\\system32");
    }
}

// POLICY GATE truoc JobManager::Submit: bat buoc client da xac thuc va path khong nam trong deniedRoots_.
// Neu bi tu choi, scan khong vao queue nen khong ton worker/engine.
bool PolicyManager::CanScan(
    const ClientIdentity& identity,
    const std::wstring& inputPath,
    std::wstring& normalizedPath,
    std::wstring& reason) const
{
    reason.clear();
    if (identity.userSid.empty())
    {
        reason = L"Unauthenticated client";
        return false;
    }
    if (inputPath.empty() || !ResolvePath(inputPath, normalizedPath))
    {
        reason = L"Invalid scan path";
        return false;
    }

    const std::wstring comparable = NormalizeCaseAndSlashes(normalizedPath);
    for (const auto& root : deniedRoots_)
    {
        if (IsUnder(comparable, root))
        {
            reason = L"Path is denied by policy: Windows\\System32";
            return false;
        }
    }
    return true;
}
