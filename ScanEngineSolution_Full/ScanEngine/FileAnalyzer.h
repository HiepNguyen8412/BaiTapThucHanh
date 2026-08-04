#pragma once

#include "../Common/EngineApi.h"

#include <Windows.h>
#include <cstdint>
#include <string>

namespace ScanEngineInternal
{
    struct FileMetadata
    {
        std::uint64_t fileSize{0};
        std::uint64_t lastWriteTime{0};
    };

    EngineStatus NormalizeFilePath(
        const wchar_t* inputPath,
        std::wstring& normalizedPath,
        DWORD& win32Error);

    EngineStatus ReadFileMetadata(
        const std::wstring& path,
        FileMetadata& metadata,
        DWORD& win32Error);

    bool IsOutsideCDrive(const std::wstring& path);
    bool HasRiskyExtension(const std::wstring& path);

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
        std::uint32_t timeoutMs);
}
