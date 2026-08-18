#include "Logger.h"

#include <Windows.h>
#include <chrono>
#include <filesystem>
#include <iomanip>

using namespace std;


bool Logger::Open(const wstring& path)
{
    lock_guard lock(mutex_);
    stream_.open(filesystem::path(path), ios::app);
    return stream_.is_open();
}

void Logger::Info(const wstring& message)
{
    Write(L"INFO", message);
}

void Logger::Error(const wstring& message)
{
    Write(L"ERROR", message);
}

void Logger::Write(const wchar_t* level, const wstring& message)
{
    lock_guard lock(mutex_);
    if (!stream_.is_open()) return;

    SYSTEMTIME time{};
    GetLocalTime(&time);
    stream_ << setfill(L'0')
        << L'[' << setw(4) << time.wYear << L'-'
        << setw(2) << time.wMonth << L'-'
        << setw(2) << time.wDay << L' '
        << setw(2) << time.wHour << L':'
        << setw(2) << time.wMinute << L':'
        << setw(2) << time.wSecond << L'.'
        << setw(3) << time.wMilliseconds << L"] "
        << L'[' << level << L"] " << message << endl;
}
