#include "Logger.h"

#include <Windows.h>
#include <chrono>
#include <filesystem>
#include <iomanip>

bool Logger::Open(const std::wstring& path)
{
    std::lock_guard lock(mutex_);
    stream_.open(std::filesystem::path(path), std::ios::app);
    return stream_.is_open();
}

void Logger::Info(const std::wstring& message)
{
    Write(L"INFO", message);
}

void Logger::Error(const std::wstring& message)
{
    Write(L"ERROR", message);
}

void Logger::Write(const wchar_t* level, const std::wstring& message)
{
    std::lock_guard lock(mutex_);
    if (!stream_.is_open()) return;

    SYSTEMTIME time{};
    GetLocalTime(&time);
    stream_ << std::setfill(L'0')
        << L'[' << std::setw(4) << time.wYear << L'-'
        << std::setw(2) << time.wMonth << L'-'
        << std::setw(2) << time.wDay << L' '
        << std::setw(2) << time.wHour << L':'
        << std::setw(2) << time.wMinute << L':'
        << std::setw(2) << time.wSecond << L'.'
        << std::setw(3) << time.wMilliseconds << L"] "
        << L'[' << level << L"] " << message << std::endl;
}
