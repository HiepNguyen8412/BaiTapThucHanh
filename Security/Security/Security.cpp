#define UNICODE
#define _UNICODE

#include <windows.h>

#include <algorithm>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include <cstdint>
#include <filesystem>

// ============================================================
// Configuration
// ============================================================

constexpr wchar_t SERVICE_NAME[] =
L"SecurityFolderMonitor";

constexpr wchar_t WATCH_DIRECTORY[] =
L"C:\\ProgramData\\SecurityFolderMonitor\\Watch";

constexpr wchar_t LOG_DIRECTORY[] =
L"C:\\ProgramData\\SecurityFolderMonitor\\Logs";

constexpr wchar_t LOG_FILE[] =
L"C:\\ProgramData\\SecurityFolderMonitor\\Logs\\service.log";

constexpr DWORD SCAN_INTERVAL_MS = 5000;

constexpr std::uintmax_t MAX_LOG_FILE_SIZE = 10ULL * 1024ULL * 1024ULL; // 10 MB
// ============================================================
// Global state
// ============================================================

SERVICE_STATUS_HANDLE g_serviceStatusHandle = nullptr;

SERVICE_STATUS g_serviceStatus{};

HANDLE g_stopEvent = nullptr;

HANDLE g_workerThread = nullptr;

std::ofstream g_log;

// ============================================================
// Utility: UTF-16 -> UTF-8
// ============================================================

std::string WideToUtf8(const std::wstring& text)
{
    if (text.empty())
        return {};

    int size = WideCharToMultiByte(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0,
        nullptr,
        nullptr
    );

    if (size <= 0)
        return {};

    std::string result(size, '\0');

    WideCharToMultiByte(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        result.data(),
        size,
        nullptr,
        nullptr
    );

    return result;
}

// ============================================================
// Utility: current local timestamp
// ============================================================

std::string GetTimestamp()
{
    SYSTEMTIME st{};
    GetLocalTime(&st);

    std::ostringstream oss;

    oss << std::setfill('0')
        << std::setw(4) << st.wYear << "-"
        << std::setw(2) << st.wMonth << "-"
        << std::setw(2) << st.wDay << " "
        << std::setw(2) << st.wHour << ":"
        << std::setw(2) << st.wMinute << ":"
        << std::setw(2) << st.wSecond;

    return oss.str();
}

std::wstring GetLogArchivePath()
{
    for (unsigned int suffix = 1; ; ++suffix)
    {
        std::filesystem::path archivePath =
            std::filesystem::path(LOG_DIRECTORY)
            / (L"ServiceOld" + std::to_wstring(suffix) + L".log");

        if (!std::filesystem::exists(archivePath))
        {
            return archivePath.wstring();
        }
    }
}

// ============================================================
// Logging
// ============================================================

bool OpenLog();

bool RotateLogIfNeeded()
{
    std::error_code error;

    // Nếu service.log chưa tồn tại thì không cần rotate
    if (!std::filesystem::exists(LOG_FILE, error))
    {
        return !error;
    }

    // Lấy kích thước service.log
    std::uintmax_t fileSize =
        std::filesystem::file_size(LOG_FILE, error);

    if (error)
        return false;

    // Chưa đạt 10 MB -> tiếp tục ghi file hiện tại
    if (fileSize < MAX_LOG_FILE_SIZE)
        return true;

    // File đã đạt 10 MB
    // Đóng file trước khi rename
    if (g_log.is_open())
    {
        g_log.flush();
        g_log.close();
    }

    // Lấy tên file cũ:
    // ServiceOld1.log
    // ServiceOld2.log
    // ServiceOld3.log...
    std::wstring archivePath =
        GetLogArchivePath();

    // service.log -> ServiceOldX.log
    std::filesystem::rename(
        LOG_FILE,
        archivePath,
        error
    );

    if (error)
        return false;

    return true;
}

void LogMessage(
    const char* level,
    const std::string& message)
{
    if (!g_log.is_open())
        return;

    // Kiểm tra service.log có đạt 10 MB chưa
    if (!RotateLogIfNeeded())
        return;

    // Nếu vừa rotate thì g_log đã bị close()
    // -> mở lại service.log mới
    if (!g_log.is_open())
    {
        g_log.open(
            LOG_FILE,
            std::ios::out | std::ios::app
        );

        if (!g_log.is_open())
            return;
    }

    // Ghi log
    g_log
        << GetTimestamp()
        << " ["
        << level
        << "] "
        << message
        << std::endl;

    g_log.flush();
}

void LogInfo(const std::string& message)
{
    LogMessage("INFO", message);
}

void LogError(const std::string& message)
{
    LogMessage("ERROR", message);
}

// ============================================================
// Open log file
// ============================================================

bool OpenLog()
{
    g_log.open(
        LOG_FILE,
        std::ios::out | std::ios::app
    );

    return g_log.is_open();
}

// ============================================================
// Update service status
// ============================================================

bool UpdateServiceStatus(
    DWORD currentState,
    DWORD win32ExitCode = NO_ERROR,
    DWORD waitHint = 0)
{
    g_serviceStatus.dwServiceType =
        SERVICE_WIN32_OWN_PROCESS;

    g_serviceStatus.dwCurrentState =
        currentState;

    g_serviceStatus.dwWin32ExitCode =
        win32ExitCode;

    g_serviceStatus.dwWaitHint =
        waitHint;

    if (currentState == SERVICE_START_PENDING)
    {
        g_serviceStatus.dwControlsAccepted = 0;
    }
    else if (currentState == SERVICE_RUNNING)
    {
        g_serviceStatus.dwControlsAccepted =
            SERVICE_ACCEPT_STOP;
    }
    else
    {
        g_serviceStatus.dwControlsAccepted = 0;
    }

    if (g_serviceStatusHandle == nullptr)
        return false;

    return SetServiceStatus(
        g_serviceStatusHandle,
        &g_serviceStatus
    ) != FALSE;
}

// ============================================================
// Build full path
// ============================================================

std::wstring BuildFullPath(
    const std::wstring& directory,
    const std::wstring& filename)
{
    if (directory.empty())
        return filename;

    if (directory.back() == L'\\')
        return directory + filename;

    return directory + L"\\" + filename;
}

// ============================================================
// Enumerate direct files in Watch directory
//
// Return value:
//   true  = enumeration succeeded
//   false = directory could not be enumerated
//
// On success:
//   files contains direct file names only.
// ============================================================

bool EnumerateFiles(
    std::set<std::wstring>& files,
    DWORD& errorCode)
{
    files.clear();
    errorCode = ERROR_SUCCESS;

    std::wstring searchPath =
        BuildFullPath(
            WATCH_DIRECTORY,
            L"*"
        );

    WIN32_FIND_DATAW findData{};

    HANDLE findHandle =
        FindFirstFileW(
            searchPath.c_str(),
            &findData
        );

    if (findHandle == INVALID_HANDLE_VALUE)
    {
        DWORD error = GetLastError();

        // An existing empty directory may produce
        // ERROR_FILE_NOT_FOUND for the wildcard.
        if (error == ERROR_FILE_NOT_FOUND)
        {
            return true;
        }

        errorCode = error;
        return false;
    }

    while (true)
    {
        const wchar_t* filename =
            findData.cFileName;

        // Ignore "." and ".."
        if (wcscmp(filename, L".") != 0 &&
            wcscmp(filename, L"..") != 0)
        {
            // Ignore directories.
            if ((findData.dwFileAttributes &
                FILE_ATTRIBUTE_DIRECTORY) == 0)
            {
                files.insert(filename);
            }
        }

        if (!FindNextFileW(
            findHandle,
            &findData))
        {
            DWORD error = GetLastError();

            if (error != ERROR_NO_MORE_FILES)
            {
                errorCode = error;

                FindClose(findHandle);
                return false;
            }

            break;
        }
    }

    FindClose(findHandle);

    return true;
}

// ============================================================
// Worker thread
// ============================================================

DWORD WINAPI WorkerThread(LPVOID)
{
    std::set<std::wstring> previousFiles;

    // false until the first successful enumeration.
    //
    // The first successful enumeration becomes
    // the baseline and is NOT logged as new files.
    bool baselineReady = false;

    // Test Log 
    for (int i = 0; i < 500000; ++i)
    {
        LogInfo("TEST LOG - SecurityFolderMonitor");
    }
    while (true)
    {
        std::set<std::wstring> currentFiles;

        DWORD errorCode = ERROR_SUCCESS;

        bool success =
            EnumerateFiles(
                currentFiles,
                errorCode
            );

        if (success)
        {
            if (!baselineReady)
            {
                // First successful scan:
                // establish baseline.
                previousFiles = currentFiles;
                baselineReady = true;
            }
            else
            {
                // currentFiles - previousFiles
                // Only files that exist now but did not
                // exist in the previous successful scan
                // are considered new.
                for (const auto& filename :
                    currentFiles)
                {
                    if (previousFiles.find(filename) ==
                        previousFiles.end())
                    {
                        std::wstring fullPath =
                            BuildFullPath(
                                WATCH_DIRECTORY,
                                filename
                            );

                        LogInfo(
                            "New file: " +
                            WideToUtf8(fullPath)
                        );
                    }
                }

                previousFiles = currentFiles;
            }
        }
        else
        {
            std::ostringstream oss;

            oss << "Cannot access watch directory. "
                << "Win32 error: "
                << errorCode;

            LogError(oss.str());

            // Important:
            //
            // Do NOT terminate the service.
            // Keep the worker alive and retry
            // during the next scan.
            //
            // We also invalidate the baseline so that
            // the first successful scan after the directory
            // becomes available again becomes a new baseline.
            baselineReady = false;
            previousFiles.clear();
        }

        // ----------------------------------------------------
        // Wait for either:
        //
        // 1. STOP event
        // 2. 5-second timeout
        //
        // This is intentionally NOT Sleep(5000).
        // If STOP is received, SetEvent() wakes this wait
        // immediately.
        // ----------------------------------------------------

        DWORD waitResult =
            WaitForSingleObject(
                g_stopEvent,
                SCAN_INTERVAL_MS
            );

        if (waitResult == WAIT_OBJECT_0)
        {
            // Stop requested.
            break;
        }

        if (waitResult == WAIT_FAILED)
        {
            DWORD waitError = GetLastError();

            std::ostringstream oss;

            oss << "WaitForSingleObject failed. "
                << "Win32 error: "
                << waitError;

            LogError(oss.str());

            // If waiting itself failed, do not spin forever.
            break;
        }

        // WAIT_TIMEOUT:
        // perform another scan.
    }

    return 0;
}

// ============================================================
// Service Control Handler
// ============================================================

DWORD WINAPI ServiceControlHandler(
    DWORD control,
    DWORD /*eventType*/,
    LPVOID /*eventData*/,
    LPVOID /*context*/)
{
    switch (control)
    {
    case SERVICE_CONTROL_STOP:
    {
        // Tell SCM that shutdown is in progress.
        UpdateServiceStatus(
            SERVICE_STOP_PENDING,
            NO_ERROR,
            5000
        );

        // Wake the worker thread immediately.
        if (g_stopEvent != nullptr)
        {
            SetEvent(g_stopEvent);
        }

        return NO_ERROR;
    }

    default:
        return NO_ERROR;
    }
}

// ============================================================
// ServiceMain
// ============================================================

void WINAPI ServiceMain(
    DWORD /*argc*/,
    LPWSTR* /*argv*/)
{
    // --------------------------------------------------------
    // 1. Register service control handler
    // --------------------------------------------------------

    g_serviceStatusHandle =
        RegisterServiceCtrlHandlerExW(
            SERVICE_NAME,
            ServiceControlHandler,
            nullptr
        );

    if (g_serviceStatusHandle == nullptr)
    {
        return;
    }

    // --------------------------------------------------------
    // 2. Initial state: START_PENDING
    // --------------------------------------------------------

    UpdateServiceStatus(
        SERVICE_START_PENDING,
        NO_ERROR,
        10000
    );

    // --------------------------------------------------------
    // 3. Create stop event
    //
    // Manual-reset event:
    //
    //   initial state = nonsignaled
    //   SetEvent()   = signaled
    // --------------------------------------------------------

    g_stopEvent =
        CreateEventW(
            nullptr,
            TRUE,   // manual-reset
            FALSE,  // initially nonsignaled
            nullptr
        );

    if (g_stopEvent == nullptr)
    {
        DWORD error = GetLastError();

        UpdateServiceStatus(
            SERVICE_STOPPED,
            error,
            0
        );

        return;
    }

    // --------------------------------------------------------
    // 4. Open log
    // --------------------------------------------------------

    if (!OpenLog())
    {
        // std::ofstream does not guarantee that
        // GetLastError() contains the relevant error.
        //
        // Therefore we simply report that opening the
        // C++ stream failed.
        UpdateServiceStatus(
            SERVICE_STOPPED,
            ERROR_OPEN_FAILED,
            0
        );

        CloseHandle(g_stopEvent);
        g_stopEvent = nullptr;

        return;
    }

    LogInfo("Service started");

    // --------------------------------------------------------
    // 5. Create worker thread
    // --------------------------------------------------------

    g_workerThread =
        CreateThread(
            nullptr,
            0,
            WorkerThread,
            nullptr,
            0,
            nullptr
        );

    if (g_workerThread == nullptr)
    {
        DWORD error = GetLastError();

        std::ostringstream oss;

        oss << "Cannot create worker thread. "
            << "Win32 error: "
            << error;

        LogError(oss.str());

        g_log.close();

        CloseHandle(g_stopEvent);
        g_stopEvent = nullptr;

        UpdateServiceStatus(
            SERVICE_STOPPED,
            error,
            0
        );

        return;
    }

    // --------------------------------------------------------
    // 6. Service is now running
    // --------------------------------------------------------

    UpdateServiceStatus(
        SERVICE_RUNNING,
        NO_ERROR,
        0
    );

    // --------------------------------------------------------
    // 7. Wait for worker thread
    //
    // The worker normally terminates after the stop event
    // has been signaled by ServiceControlHandler.
    // --------------------------------------------------------

    DWORD waitResult =
        WaitForSingleObject(
            g_workerThread,
            INFINITE
        );

    if (waitResult == WAIT_FAILED)
    {
        DWORD error = GetLastError();

        std::ostringstream oss;

        oss << "Waiting for worker thread failed. "
            << "Win32 error: "
            << error;

        LogError(oss.str());
    }

    // --------------------------------------------------------
    // 8. Cleanup
    // --------------------------------------------------------

    if (g_workerThread != nullptr)
    {
        CloseHandle(g_workerThread);
        g_workerThread = nullptr;
    }

    if (g_stopEvent != nullptr)
    {
        CloseHandle(g_stopEvent);
        g_stopEvent = nullptr;
    }

    LogInfo("Service stopped");

    if (g_log.is_open())
    {
        g_log.close();
    }

    // --------------------------------------------------------
    // 9. Final service state
    // --------------------------------------------------------

    UpdateServiceStatus(
        SERVICE_STOPPED,
        NO_ERROR,
        0
    );
}

// ============================================================
// Service table / process entry point
// ============================================================

int wmain()
{
    SERVICE_TABLE_ENTRYW serviceTable[] =
    {
        {
            const_cast<LPWSTR>(SERVICE_NAME),
            ServiceMain
        },
        {
            nullptr,
            nullptr
        }
    };

    // Connect this process to SCM.
    //
    // If this executable is started by SCM as a service,
    // SCM will invoke ServiceMain().
    //
    // If the call fails because this executable was launched
    // directly from a console, GetLastError() commonly
    // returns ERROR_FAILED_SERVICE_CONTROLLER_CONNECT.
    //

    if (!StartServiceCtrlDispatcherW(
        serviceTable))
    {
        DWORD error = GetLastError();

        if (error ==
            ERROR_FAILED_SERVICE_CONTROLLER_CONNECT)
        {
            std::wcerr
                << L"This program must be started "
                L"by the Service Control Manager."
                << std::endl;

            return 1;
        }

        std::wcerr
            << L"StartServiceCtrlDispatcherW failed. "
            << L"Win32 error: "
            << error
            << std::endl;

        return 1;
    }

    return 0;
}
