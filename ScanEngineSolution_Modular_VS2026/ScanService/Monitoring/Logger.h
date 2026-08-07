// ============================================================================
// MODULE : ScanService / Monitoring
// ROLE   : Logger don gian, thread-safe.
// NOTE   : File duoc sap xep lai theo kien truc module de de doc va thuyet trinh.
// ============================================================================

#pragma once

#include <fstream>
#include <mutex>
#include <string>

class Logger
{
public:
    bool Open(const std::wstring& path);
    void Info(const std::wstring& message);
    void Error(const std::wstring& message);

private:
    void Write(const wchar_t* level, const std::wstring& message);
    std::mutex mutex_;
    std::wofstream stream_;
};
