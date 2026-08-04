#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include "ServiceApp.h"
#include "../Common/WinUtil.h"

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

    DWORD WINAPI ServiceControlHandler(DWORD control, DWORD, void*, void*)
    {
        if (control == SERVICE_CONTROL_STOP || control == SERVICE_CONTROL_SHUTDOWN)
        {
            SetServiceState(SERVICE_STOP_PENDING, NO_ERROR, 5000);
            if (g_stopEvent != nullptr) SetEvent(g_stopEvent);
        }
        return NO_ERROR;
    }

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

    BOOL WINAPI ConsoleHandler(DWORD control)
    {
        if (control == CTRL_C_EVENT || control == CTRL_BREAK_EVENT || control == CTRL_CLOSE_EVENT)
        {
            if (g_stopEvent != nullptr) SetEvent(g_stopEvent);
            return TRUE;
        }
        return FALSE;
    }

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
