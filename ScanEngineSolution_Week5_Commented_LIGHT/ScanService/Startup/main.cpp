// ============================================================================
// MODULE : ScanService / Startup
// ROLE   : Windows Service/console entry point va Service Control lifecycle.

// ============================================================================

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include "Startup/ServiceApp.h"
#include "Platform/WinUtil.h"

#include <iostream>
#include <iterator>
#include <string>

namespace
{
    constexpr wchar_t SERVICE_NAME[] = L"AvScanService";
    SERVICE_STATUS_HANDLE g_statusHandle = nullptr;
    SERVICE_STATUS g_status{};
    HANDLE g_stopEvent = nullptr;
    ServiceApp* g_app = nullptr;

    // Bao trang thai hien tai cho Windows Service Control Manager (SCM).
    // Vi du: START_PENDING -> RUNNING -> STOP_PENDING -> STOPPED.
    void SetServiceState(DWORD state, DWORD win32ExitCode = NO_ERROR, DWORD waitHint = 0)
    {
        g_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
        g_status.dwCurrentState = state;
        g_status.dwWin32ExitCode = win32ExitCode;
        g_status.dwWaitHint = waitHint;
        g_status.dwControlsAccepted = state == SERVICE_START_PENDING
            ? 0
            : SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
        SetServiceStatus(g_statusHandle, &g_status);
    }

    // SCM goi ham nay khi co lenh STOP/SHUTDOWN; ta chi danh dau stop va danh thuc luong chinh.
    DWORD WINAPI ServiceControlHandler(DWORD control, DWORD, void*, void*)
    {
        if (control == SERVICE_CONTROL_STOP || control == SERVICE_CONTROL_SHUTDOWN)
        {
            SetServiceState(SERVICE_STOP_PENDING, NO_ERROR, 5000);
            if (g_stopEvent != nullptr) SetEvent(g_stopEvent);
        }
        return NO_ERROR;
    }

    // Entry point khi chuong trinh duoc SCM khoi dong nhu Windows Service.
    // Dang ky control handler, Start ServiceApp, sau do cho den khi co yeu cau dung.
    void WINAPI ServiceMain(DWORD, wchar_t**)
    {
        g_statusHandle = RegisterServiceCtrlHandlerExW(
            SERVICE_NAME,
            ServiceControlHandler,
            nullptr);
        if (g_statusHandle == nullptr) return;

        SetServiceState(SERVICE_START_PENDING, NO_ERROR, 5000);
        g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (g_stopEvent == nullptr)
        {
            SetServiceState(SERVICE_STOPPED, GetLastError());
            return;
        }

        ServiceApp app;
        g_app = &app;
        if (!app.Start())
        {
            SetServiceState(SERVICE_STOPPED, ERROR_SERVICE_SPECIFIC_ERROR);
            CloseHandle(g_stopEvent);
            g_stopEvent = nullptr;
            g_app = nullptr;
            return;
        }

        SetServiceState(SERVICE_RUNNING);
        WaitForSingleObject(g_stopEvent, INFINITE);
        app.Stop();
        g_app = nullptr;
        CloseHandle(g_stopEvent);
        g_stopEvent = nullptr;
        SetServiceState(SERVICE_STOPPED);
    }

    // Xu ly Ctrl+C/Ctrl+Break khi chay che do console de dung ServiceApp gon gang.
    BOOL WINAPI ConsoleHandler(DWORD control)
    {
        if (control == CTRL_C_EVENT || control == CTRL_BREAK_EVENT || control == CTRL_CLOSE_EVENT)
        {
            if (g_stopEvent != nullptr) SetEvent(g_stopEvent);
            return TRUE;
        }
        return FALSE;
    }

    // Che do test/debug: chay cung ServiceApp nhung khong can cai vao SCM.
    int RunConsole()
    {
        std::wcout << L"AvScanService console mode. Press Ctrl+C to stop.\n";
        g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        SetConsoleCtrlHandler(ConsoleHandler, TRUE);
        ServiceApp app;
        g_app = &app;
        if (!app.Start())
        {
            std::wcerr << L"Service startup failed. Check ScanService.log.\n";
            CloseHandle(g_stopEvent);
            g_stopEvent = nullptr;
            g_app = nullptr;
            return 1;
        }
        WaitForSingleObject(g_stopEvent, INFINITE);
        app.Stop();
        g_app = nullptr;
        CloseHandle(g_stopEvent);
        g_stopEvent = nullptr;
        return 0;
    }

    // Tao service trong SCM va tro ImagePath den chinh file executable nay.
    bool InstallService()
    {
        wchar_t executable[32768]{};
        if (GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable))) == 0)
        {
            return false;
        }
        std::wstring quoted = L"\"" + std::wstring(executable) + L"\"";
        SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
        if (manager == nullptr) return false;
        SC_HANDLE service = CreateServiceW(
            manager,
            SERVICE_NAME,
            L"AV Scan Engine Service",
            SERVICE_ALL_ACCESS,
            SERVICE_WIN32_OWN_PROCESS,
            SERVICE_DEMAND_START,
            SERVICE_ERROR_NORMAL,
            quoted.c_str(),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr);
        if (service == nullptr)
        {
            CloseServiceHandle(manager);
            return false;
        }
        CloseServiceHandle(service);
        CloseServiceHandle(manager);
        return true;
    }

    // Mo service da cai va goi DeleteService de go bo dang ky khoi SCM.
    bool UninstallService()
    {
        SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
        if (manager == nullptr) return false;
        SC_HANDLE service = OpenServiceW(manager, SERVICE_NAME, DELETE | SERVICE_STOP | SERVICE_QUERY_STATUS);
        if (service == nullptr)
        {
            CloseServiceHandle(manager);
            return false;
        }
        SERVICE_STATUS status{};
        ControlService(service, SERVICE_CONTROL_STOP, &status);
        const BOOL deleted = DeleteService(service);
        CloseServiceHandle(service);
        CloseServiceHandle(manager);
        return deleted != FALSE;
    }
}

// Parse command line: console/install/uninstall; neu khong co tham so thi vao ServiceMain.
int wmain(int argc, wchar_t* argv[])
{
    if (argc >= 2)
    {
        const std::wstring command = argv[1];
        if (command == L"--console") return RunConsole();
        if (command == L"install")
        {
            if (InstallService())
            {
                std::wcout << L"Service installed. Use: sc start AvScanService\n";
                return 0;
            }
            std::wcerr << L"Install failed: " << WinUtil::GetLastErrorMessage(GetLastError()) << L'\n';
            return 1;
        }
        if (command == L"uninstall")
        {
            if (UninstallService())
            {
                std::wcout << L"Service removed.\n";
                return 0;
            }
            std::wcerr << L"Uninstall failed: " << WinUtil::GetLastErrorMessage(GetLastError()) << L'\n';
            return 1;
        }
    }

    SERVICE_TABLE_ENTRYW table[] = {
        {const_cast<LPWSTR>(SERVICE_NAME), ServiceMain},
        {nullptr, nullptr}};
    if (!StartServiceCtrlDispatcherW(table))
    {
        std::wcerr << L"StartServiceCtrlDispatcher failed. Run with --console for debugging. Error="
            << GetLastError() << L'\n';
        return 1;
    }
    return 0;
}
