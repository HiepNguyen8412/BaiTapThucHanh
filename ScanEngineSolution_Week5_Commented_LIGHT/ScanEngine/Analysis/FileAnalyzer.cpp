// ============================================================================
// MODULE : ScanEngine / Analysis
// ROLE   : Mo file, doc block 64 KiB, tinh Shannon entropy va report progress.

// ============================================================================

#include "Analysis/FileAnalyzer.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cwctype>
#include <vector>

namespace
{
    constexpr DWORD READ_BUFFER_SIZE = 64u * 1024u;

    // Chuyen Win32 file error sang EngineStatus on dinh de Service/Client khong phu thuoc ma loi thap cap.
    EngineStatus MapFileError(DWORD errorCode)
    {
        switch (errorCode)
        {
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
        case ERROR_INVALID_NAME:
            return EngineStatus::FileNotFound;
        case ERROR_ACCESS_DENIED:
        case ERROR_SHARING_VIOLATION:
            return EngineStatus::AccessDenied;
        default:
            return EngineStatus::OpenFileFailed;
        }
    }

    // Kiem tra timeout theo GetTickCount64; timeoutMs=0 duoc hieu la khong gioi han.
    bool HasTimedOut(ULONGLONG scanStartTick, std::uint32_t timeoutMs)
    {
        return timeoutMs != 0 && (GetTickCount64() - scanStartTick) >= timeoutMs;
    }

    bool ReportProgress(
        EngineProgressCallback callback,
        void* userContext,
        EngineScanStage stage,
        std::uint32_t percent,
        const wchar_t* message)
    {
        if (callback == nullptr)
        {
            return true;
        }
        EngineProgressInfoV1 info{};
        info.structSize = sizeof(info);
        info.apiVersion = ENGINE_API_VERSION_1;
        info.eventType = EngineEventType::Progress;
        info.stage = stage;
        info.progressPercent = (std::min)(percent, 100u);
        info.status = EngineStatus::Success;
        info.result = nullptr;
        info.message = message;
        return callback(&info, userContext) != FALSE;
    }

    bool IsPathSeparator(wchar_t ch)
    {
        return ch == L'\\' || ch == L'/';
    }
}

namespace ScanEngineInternal
{
    // Xac minh input va chuan hoa duong dan file truoc khi mo/doc metadata.
    EngineStatus NormalizeFilePath(
        const wchar_t* inputPath,
        std::wstring& normalizedPath,
        DWORD& win32Error)
    {
        normalizedPath.clear();
        win32Error = ERROR_SUCCESS;
        if (inputPath == nullptr || inputPath[0] == L'\0')
        {
            return EngineStatus::InvalidArgument;
        }

        const DWORD required = GetFullPathNameW(inputPath, 0, nullptr, nullptr);
        if (required == 0)
        {
            win32Error = GetLastError();
            return EngineStatus::InvalidArgument;
        }

        std::vector<wchar_t> buffer(static_cast<std::size_t>(required) + 1, L'\0');
        const DWORD actual = GetFullPathNameW(
            inputPath,
            static_cast<DWORD>(buffer.size()),
            buffer.data(),
            nullptr);
        if (actual == 0)
        {
            win32Error = GetLastError();
            return EngineStatus::InvalidArgument;
        }
        if (actual >= buffer.size())
        {
            win32Error = ERROR_INSUFFICIENT_BUFFER;
            return EngineStatus::InternalError;
        }

        normalizedPath.assign(buffer.data(), actual);
        std::replace(normalizedPath.begin(), normalizedPath.end(), L'/', L'\\');
        return EngineStatus::Success;
    }

    // Doc fileSize + lastWriteTime. Service cung dung 2 thong tin nay de tao cache identity.
    EngineStatus ReadFileMetadata(
        const std::wstring& path,
        FileMetadata& metadata,
        DWORD& win32Error)
    {
        metadata = {};
        win32Error = ERROR_SUCCESS;
        if (path.empty())
        {
            return EngineStatus::InvalidArgument;
        }

        WIN32_FILE_ATTRIBUTE_DATA data{};
        if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data))
        {
            win32Error = GetLastError();
            return MapFileError(win32Error);
        }
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        {
            win32Error = ERROR_DIRECTORY;
            return EngineStatus::InvalidArgument;
        }

        ULARGE_INTEGER size{};
        size.HighPart = data.nFileSizeHigh;
        size.LowPart = data.nFileSizeLow;
        metadata.fileSize = size.QuadPart;

        ULARGE_INTEGER time{};
        time.HighPart = data.ftLastWriteTime.dwHighDateTime;
        time.LowPart = data.ftLastWriteTime.dwLowDateTime;
        metadata.lastWriteTime = time.QuadPart;
        return EngineStatus::Success;
    }

    // Rule demo: kiem tra file nam ngoai C:\ de tang risk score.
    bool IsOutsideCDrive(const std::wstring& path)
    {
        if (path.empty())
        {
            return true;
        }
        std::size_t drivePosition = 0;
        if (path.size() >= 7 &&
            path[0] == L'\\' && path[1] == L'\\' &&
            (path[2] == L'?' || path[2] == L'.') &&
            path[3] == L'\\')
        {
            drivePosition = 4;
        }
        if (path.size() < drivePosition + 3 ||
            path[drivePosition + 1] != L':' ||
            !IsPathSeparator(path[drivePosition + 2]))
        {
            return true;
        }
        const wchar_t driveLetter = static_cast<wchar_t>(
            std::towupper(path[drivePosition]));
        return driveLetter != L'C';
    }

    // Rule demo: nhan dien extension co kha nang thuc thi/script nhu exe,dll,sys,js,vbs,ps1.
    bool HasRiskyExtension(const std::wstring& path)
    {
        const auto slash = path.find_last_of(L"\\/");
        const auto dot = path.find_last_of(L'.');
        if (dot == std::wstring::npos ||
            (slash != std::wstring::npos && dot < slash))
        {
            return false;
        }
        std::wstring extension = path.substr(dot);
        std::transform(
            extension.begin(),
            extension.end(),
            extension.begin(),
            [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });

        static const std::array<std::wstring, 6> risky{
            L".exe", L".dll", L".sys", L".js", L".vbs", L".ps1"};
        return std::find(risky.begin(), risky.end(), extension) != risky.end();
    }

    // Doc toi da maxBytes; moi lan doc toi da READ_BUFFER_SIZE (64 KiB).
    // Callback duoc goi trong loop de stream progress va cooperative cancellation.
    // Doc noi dung file theo chunk, cap nhat tan suat byte va tinh Shannon entropy.
    // Trong vong lap co callback progress + cancel + timeout de scan file lon van phan hoi tot.
    EngineStatus CalculateFileEntropy(
        const std::wstring& path,
        std::uint64_t maxBytes,
        double& entropy,
        DWORD& win32Error,
        EngineProgressCallback callback,
        void* userContext,
        std::uint32_t startProgress,
        std::uint32_t endProgress,
        std::uint32_t progressIntervalMs,
        ULONGLONG scanStartTick,
        std::uint32_t timeoutMs)
    {
        entropy = 0.0;
        win32Error = ERROR_SUCCESS;
        if (path.empty() || maxBytes == 0 || startProgress > endProgress || endProgress > 100)
        {
            return EngineStatus::InvalidArgument;
        }
        if (scanStartTick == 0)
        {
            scanStartTick = GetTickCount64();
        }
        if (HasTimedOut(scanStartTick, timeoutMs))
        {
            return EngineStatus::Timeout;
        }

        HANDLE file = CreateFileW(
            path.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
            nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            win32Error = GetLastError();
            return MapFileError(win32Error);
        }

        LARGE_INTEGER size{};
        if (!GetFileSizeEx(file, &size) || size.QuadPart < 0)
        {
            win32Error = GetLastError();
            if (win32Error == ERROR_SUCCESS) win32Error = ERROR_INVALID_DATA;
            CloseHandle(file);
            return EngineStatus::ReadFileFailed;
        }

        const auto fileSize = static_cast<std::uint64_t>(size.QuadPart);
        const std::uint64_t bytesToRead = (std::min)(fileSize, maxBytes);
        if (!ReportProgress(
                callback,
                userContext,
                EngineScanStage::ReadingContent,
                startProgress,
                L"Reading file content"))
        {
            CloseHandle(file);
            return EngineStatus::Cancelled;
        }

        if (bytesToRead == 0)
        {
            CloseHandle(file);
            return ReportProgress(
                callback,
                userContext,
                EngineScanStage::ReadingContent,
                endProgress,
                L"Empty file")
                ? EngineStatus::Success
                : EngineStatus::Cancelled;
        }

        std::array<std::uint64_t, 256> frequencies{};
        std::vector<unsigned char> buffer(READ_BUFFER_SIZE);
        std::uint64_t totalRead = 0;
        ULONGLONG lastProgressTick = GetTickCount64();

        while (totalRead < bytesToRead)
        {
            if (HasTimedOut(scanStartTick, timeoutMs))
            {
                CloseHandle(file);
                return EngineStatus::Timeout;
            }

            const std::uint64_t remaining = bytesToRead - totalRead;
            const DWORD requestSize = static_cast<DWORD>(
                (std::min)(static_cast<std::uint64_t>(buffer.size()), remaining));
            DWORD bytesRead = 0;
            if (!ReadFile(file, buffer.data(), requestSize, &bytesRead, nullptr))
            {
                win32Error = GetLastError();
                CloseHandle(file);
                return EngineStatus::ReadFileFailed;
            }
            if (bytesRead == 0)
            {
                break;
            }
            for (DWORD i = 0; i < bytesRead; ++i)
            {
                ++frequencies[buffer[i]];
            }
            totalRead += bytesRead;

            const ULONGLONG now = GetTickCount64();
            const bool reportNow = progressIntervalMs == 0 ||
                now - lastProgressTick >= progressIntervalMs ||
                totalRead >= bytesToRead;
            if (reportNow)
            {
                const double ratio = static_cast<double>(totalRead) /
                    static_cast<double>(bytesToRead);
                const auto percent = startProgress + static_cast<std::uint32_t>(
                    ratio * static_cast<double>(endProgress - startProgress));
                if (!ReportProgress(
                        callback,
                        userContext,
                        EngineScanStage::ReadingContent,
                        (std::min)(percent, endProgress),
                        L"Reading and analyzing file"))
                {
                    CloseHandle(file);
                    return EngineStatus::Cancelled;
                }
                lastProgressTick = now;
            }
        }
        CloseHandle(file);

        if (totalRead == 0)
        {
            return EngineStatus::Success;
        }
        if (!ReportProgress(
                callback,
                userContext,
                EngineScanStage::CalculatingEntropy,
                endProgress,
                L"Calculating entropy"))
        {
            return EngineStatus::Cancelled;
        }

        double calculated = 0.0;
        for (const auto frequency : frequencies)
        {
            if (frequency == 0) continue;
            const double probability = static_cast<double>(frequency) /
                static_cast<double>(totalRead);
            calculated -= probability * std::log2(probability);
        }
        entropy = calculated;
        return EngineStatus::Success;
    }
}
