// ============================================================================
// MODULE : Common / Platform
// ROLE   : Cai dat cac tien ich Windows dung chung.

// ============================================================================

#include "WinUtil.h"

#include <Lmcons.h>
#include <cwctype>
#include <iterator>
#include <vector>

namespace WinUtil
{
    // Chuyen ma loi Win32 thanh chuoi de log/hien thi cho nguoi dung.
    // FormatMessageW tu dong lay noi dung loi tu he thong Windows.
    std::wstring GetLastErrorMessage(DWORD errorCode)
    {
        wchar_t* messageBuffer = nullptr;
        const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER |
            FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS;
        const DWORD length = FormatMessageW(
            flags,
            nullptr,
            errorCode,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            reinterpret_cast<wchar_t*>(&messageBuffer),
            0,
            nullptr);
        std::wstring message;
        if (length != 0 && messageBuffer != nullptr)
        {
            message.assign(messageBuffer, length);
            LocalFree(messageBuffer);
        }
        return message;
    }

    // Lay ten user Windows hien tai. Thong tin nay duoc client gui trong HELLO
    // va duoc Service doi chieu voi token that de tranh client gia mao danh tinh.
    std::wstring GetCurrentUserNameString()
    {
        wchar_t buffer[UNLEN + 1]{};
        DWORD length = static_cast<DWORD>(std::size(buffer));
        if (GetUserNameW(buffer, &length))
        {
            return buffer;
        }
        return L"unknown";
    }

    // Lay thu muc chua file EXE dang chay, dung lam moc de tim DLL/cache/log.
    std::wstring GetExecutableDirectory()
    {
        std::vector<wchar_t> buffer(32768, L'\0');
        const DWORD length = GetModuleFileNameW(
            nullptr,
            buffer.data(),
            static_cast<DWORD>(buffer.size()));
        if (length == 0 || length >= buffer.size())
        {
            return L".";
        }
        std::wstring path(buffer.data(), length);
        const auto slash = path.find_last_of(L"\\/");
        return slash == std::wstring::npos ? L"." : path.substr(0, slash);
    }

    // Ghep 2 thanh phan duong dan va dam bao chi co 1 dau phan cach o giua.
    std::wstring JoinPath(const std::wstring& left, const std::wstring& right)
    {
        if (left.empty()) return right;
        if (right.empty()) return left;
        if (left.back() == L'\\' || left.back() == L'/')
        {
            return left + right;
        }
        return left + L"\\" + right;
    }

    // Chuan hoa path ve dang tuyet doi/canonical truoc khi policy, cache va engine su dung.
    // Muc dich: cung mot file khong bi xem la nhieu key chi vi cach viet path khac nhau.
    std::wstring NormalizePath(const std::wstring& path, DWORD& errorCode)
    {
        errorCode = ERROR_SUCCESS;
        if (path.empty())
        {
            errorCode = ERROR_INVALID_PARAMETER;
            return {};
        }
        DWORD required = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
        if (required == 0)
        {
            errorCode = GetLastError();
            return {};
        }
        std::vector<wchar_t> buffer(static_cast<std::size_t>(required) + 1, L'\0');
        DWORD actual = GetFullPathNameW(
            path.c_str(),
            static_cast<DWORD>(buffer.size()),
            buffer.data(),
            nullptr);
        if (actual == 0 || actual >= buffer.size())
        {
            errorCode = actual == 0 ? GetLastError() : ERROR_INSUFFICIENT_BUFFER;
            return {};
        }
        std::wstring result(buffer.data(), actual);
        for (auto& ch : result)
        {
            if (ch == L'/') ch = L'\\';
            else ch = static_cast<wchar_t>(towlower(ch));
        }
        return result;
    }

    // Lay "danh tinh" cua file = size + lastWriteTime.
    // Hai gia tri nay ket hop voi path/version tao cache key de phat hien file da thay doi.
    bool GetFileIdentity(
        const std::wstring& path,
        std::uint64_t& fileSize,
        std::uint64_t& lastWriteTime,
        DWORD& errorCode)
    {
        fileSize = 0;
        lastWriteTime = 0;
        errorCode = ERROR_SUCCESS;
        WIN32_FILE_ATTRIBUTE_DATA data{};
        if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data))
        {
            errorCode = GetLastError();
            return false;
        }
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        {
            errorCode = ERROR_DIRECTORY;
            return false;
        }
        ULARGE_INTEGER size{};
        size.HighPart = data.nFileSizeHigh;
        size.LowPart = data.nFileSizeLow;
        fileSize = size.QuadPart;
        ULARGE_INTEGER time{};
        time.HighPart = data.ftLastWriteTime.dwHighDateTime;
        time.LowPart = data.ftLastWriteTime.dwLowDateTime;
        lastWriteTime = time.QuadPart;
        return true;
    }
}
