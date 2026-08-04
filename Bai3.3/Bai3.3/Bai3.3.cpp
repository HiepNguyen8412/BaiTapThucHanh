#include <windows.h>
#include <tchar.h>
#include <iostream>
#include <fstream>
#include <string>
#include <iomanip>
#include <cstdio>
#include <ctime>
#include <sstream>

using namespace std;

#pragma comment(lib, "advapi32.lib")

#define SERVICE_NAME _T("SysMonitorService")

//-----------------------------
// Log Config
//-----------------------------

const string LOG_FILE_PATH =
"C:\\SysMonitor\\SysMonitorLog.txt";
const string LOG_DIR_PATH =
"C:\\SysMonitor";

const ULONGLONG LOG_MAX_SIZE = 1024 * 1024;      // 1 MB
const int LOG_MAX_DAYS = 5;

//-----------------------------
// Service Globals
//-----------------------------

SERVICE_STATUS g_ServiceStatus = { 0 };
SERVICE_STATUS_HANDLE g_StatusHandle = NULL;

HANDLE g_ServiceStopEvent = NULL;

//-----------------------------
// CPU tracking state
//-----------------------------

struct CPU_TIME_STATE {
    ULONGLONG idleTime = 0;
    ULONGLONG kernelTime = 0;
    ULONGLONG userTime = 0;
    bool initialized = false;
};

CPU_TIME_STATE g_LastCpuTime;
volatile bool g_ShouldStop = false;
bool g_ConsoleMode = false;   // true = dang chay nhu 1 console app binh thuong (khong qua SCM)

//-----------------------------
// Khai bao cac ham
//-----------------------------
VOID WINAPI ServiceMain(DWORD argc, LPTSTR* argv);
BOOL WINAPI ConsoleHandler(DWORD);
VOID WINAPI ServiceCtrlHandler(DWORD);

void InstallService();
void UninstallService();
void LogRotation();
void WriteLog(const string& message);
string GetSystemMetrics();
double GetCpuUsage();
double GetDiskUsage();
ULONGLONG FileTimeToULL(const FILETIME& ft);

void RunMonitorLoop();
void RunConsoleMode();
void PrintUsage();

int _tmain(int argc, TCHAR* argv[]) {

    if (argc > 1 && lstrcmpi(argv[1], _T("install")) == 0) {
        InstallService();
        return 0;
    }

    if (argc > 1 && lstrcmpi(argv[1], _T("uninstall")) == 0) {
        UninstallService();
        return 0;
    }

    if (argc > 1 && (lstrcmpi(argv[1], _T("console")) == 0 || lstrcmpi(argv[1], _T("run")) == 0)) {
        RunConsoleMode();
        return 0;
    }

    if (argc > 1 && (lstrcmpi(argv[1], _T("/?")) == 0 || lstrcmpi(argv[1], _T("help")) == 0)) {
        PrintUsage();
        return 0;
    }

    // Khong truyen tham so: thu khoi dong nhu mot Windows Service that su.
    // Neu that bai vi khong duoc SCM khoi chay (loi 1063 - ERROR_FAILED_SERVICE_CONTROLLER_CONNECT),
    // nghia la dang chay truc tiep tu cmd -> tu dong chuyen sang che do console
    // thay vi bao loi roi thoat.
    
    SERVICE_TABLE_ENTRY ServiceTable[] = {
        { (LPTSTR)SERVICE_NAME, (LPSERVICE_MAIN_FUNCTION)ServiceMain },
        { NULL, NULL }
    };
    //Kết nối chương trình với SCM 
    if (!StartServiceCtrlDispatcher(ServiceTable)) { 
        DWORD err = GetLastError();
        if (err == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) {
            RunConsoleMode();
        }
        else {
            cout << "StartServiceCtrlDispatcher that bai. Ma loi: " << err << endl;
        }
    }

	return 0;
}

//========Bước 2========
// Ham xu ly su kien Ctrl+C, Ctrl+Break, Close Console, Logoff, Shutdown
BOOL WINAPI ConsoleHandler(DWORD ctrlType) {
    if (g_ServiceStopEvent == NULL)
        return FALSE;

    switch (ctrlType) {
    case CTRL_C_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        g_ShouldStop = true;
        SetEvent(g_ServiceStopEvent);
        return TRUE;
    default:
        return FALSE;
    }
}

VOID WINAPI ServiceMain(DWORD argc, LPTSTR* argv)
{
    UNREFERENCED_PARAMETER(argc);//Tắt cảnh báo tham số không dùng tới
    UNREFERENCED_PARAMETER(argv);

	g_StatusHandle = RegisterServiceCtrlHandler(SERVICE_NAME, ServiceCtrlHandler); //Đăng kí hàm xử lý điều khiển dịch vụ
    if (g_StatusHandle == NULL) {
        return;
    }

    g_ServiceStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (g_ServiceStopEvent == NULL) {
        return;
    }

    g_ShouldStop = false;
    g_ConsoleMode = false;

    g_ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_ServiceStatus.dwCurrentState = SERVICE_START_PENDING;
    g_ServiceStatus.dwControlsAccepted = 0;
    g_ServiceStatus.dwWin32ExitCode = NO_ERROR;
    g_ServiceStatus.dwServiceSpecificExitCode = 0;
    g_ServiceStatus.dwCheckPoint = 1;
    g_ServiceStatus.dwWaitHint = 30000;
    //Sét trạng thái
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);

    g_ServiceStatus.dwCurrentState = SERVICE_RUNNING;
    g_ServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;
    g_ServiceStatus.dwWin32ExitCode = NO_ERROR;
    g_ServiceStatus.dwServiceSpecificExitCode = 0;
    g_ServiceStatus.dwCheckPoint = 0;
    g_ServiceStatus.dwWaitHint = 0;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);

    WriteLog("Service started.");

    RunMonitorLoop();

    WriteLog("Service stopping.");

    // QUAN TRONG: bao cho SCM biet service DA THUC SU dung han.
    // Ban goc thieu buoc nay -> SCM cho mai khong thay xac nhan -> timeout (1053 / Event 7009).
    g_ServiceStatus.dwControlsAccepted = 0;
    g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
    g_ServiceStatus.dwWin32ExitCode = NO_ERROR;
    g_ServiceStatus.dwCheckPoint = 3;
    g_ServiceStatus.dwWaitHint = 0;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);

    CloseHandle(g_ServiceStopEvent);
    g_ServiceStopEvent = NULL;
}

VOID WINAPI ServiceCtrlHandler(DWORD CtrlCode) {
    switch (CtrlCode) {
    case SERVICE_CONTROL_STOP:
        if (g_ServiceStatus.dwCurrentState != SERVICE_RUNNING) {
            break;
        }

        // Bao SCM biet dang trong qua trinh dung (STOP_PENDING) TRUOC KHI don dep.
        // Day la buoc bi thieu trong ban goc.
        g_ServiceStatus.dwControlsAccepted = 0;
        g_ServiceStatus.dwCurrentState = SERVICE_STOP_PENDING;
        g_ServiceStatus.dwWin32ExitCode = NO_ERROR;
        g_ServiceStatus.dwCheckPoint = 4;
        g_ServiceStatus.dwWaitHint = 5000;
        SetServiceStatus(g_StatusHandle, &g_ServiceStatus);

        g_ShouldStop = true;
        if (g_ServiceStopEvent != NULL) {
            SetEvent(g_ServiceStopEvent);
        }
        break;

    default:
        break;
    }
}

// ---------------- CAC HAM TIEN ICH ---------------- //

ULONGLONG FileTimeToULL(const FILETIME& ft) {
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    return uli.QuadPart;
}

double GetCpuUsage() {
    FILETIME idleTime, kernelTime, userTime;
    if (!GetSystemTimes(&idleTime, &kernelTime, &userTime)) {
        return 0.0;
    }

    const ULONGLONG idle = FileTimeToULL(idleTime);
    const ULONGLONG kernel = FileTimeToULL(kernelTime);
    const ULONGLONG user = FileTimeToULL(userTime);

    if (!g_LastCpuTime.initialized) {
        g_LastCpuTime.idleTime = idle;
        g_LastCpuTime.kernelTime = kernel;
        g_LastCpuTime.userTime = user;
        g_LastCpuTime.initialized = true;
        return 0.0;
    }

    const ULONGLONG idleDiff = idle - g_LastCpuTime.idleTime;
    const ULONGLONG kernelDiff = kernel - g_LastCpuTime.kernelTime;
    const ULONGLONG userDiff = user - g_LastCpuTime.userTime;
    const ULONGLONG totalDiff = kernelDiff + userDiff;

    g_LastCpuTime.idleTime = idle;
    g_LastCpuTime.kernelTime = kernel;
    g_LastCpuTime.userTime = user;

    if (totalDiff == 0) {
        return 0.0;
    }

    return 100.0 * (1.0 - static_cast<double>(idleDiff) / static_cast<double>(totalDiff));
}

// Lấy thông tin Disk Usage
double GetDiskUsage() {
    ULARGE_INTEGER freeBytesAvailableToCaller;
    ULARGE_INTEGER totalNumberOfBytes;
    ULARGE_INTEGER totalNumberOfFreeBytes;

    if (!GetDiskFreeSpaceExA("C:\\", &freeBytesAvailableToCaller, &totalNumberOfBytes, &totalNumberOfFreeBytes)) {
        return 0.0;
    }

    if (totalNumberOfBytes.QuadPart == 0) {
        return 0.0;
    }

    return 100.0 * (1.0 - static_cast<double>(totalNumberOfFreeBytes.QuadPart) / static_cast<double>(totalNumberOfBytes.QuadPart));
}

// Lấy thông tin RAM, Cpu, Disk
string GetSystemMetrics() {
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    GlobalMemoryStatusEx(&memInfo);
    DWORD ramUsage = memInfo.dwMemoryLoad;

    const double cpuValue = GetCpuUsage();
    const double diskValue = GetDiskUsage();

    char buffer[256];
    snprintf(buffer, sizeof(buffer), "RAM: %lu%% | CPU: %.2f%% | DISK: %.2f%%", ramUsage, cpuValue, diskValue);
    return string(buffer);
}

//Kiểm tra file log
void LogRotation() {
    WIN32_FILE_ATTRIBUTE_DATA attr;
    if (!GetFileAttributesExA(LOG_FILE_PATH.c_str(), GetFileExInfoStandard, &attr)) {
        return;
    }

    bool shouldRotate = false;

    ULARGE_INTEGER fileSize;
    fileSize.LowPart = attr.nFileSizeLow;
    fileSize.HighPart = attr.nFileSizeHigh;
    if (fileSize.QuadPart >= LOG_MAX_SIZE) {
        shouldRotate = true;
    }

    FILETIME ftNow;
    GetSystemTimeAsFileTime(&ftNow);

    FILETIME ftWrite = attr.ftLastWriteTime;
    SYSTEMTIME stWrite;
    FileTimeToSystemTime(&ftWrite, &stWrite);

    FILETIME ftLocal;
    SystemTimeToFileTime(&stWrite, &ftLocal);
    ULARGE_INTEGER writeTime;
    writeTime.LowPart = ftLocal.dwLowDateTime;
    writeTime.HighPart = ftLocal.dwHighDateTime;

    ULARGE_INTEGER nowTime;
    nowTime.LowPart = ftNow.dwLowDateTime;
    nowTime.HighPart = ftNow.dwHighDateTime;

    const long long ageHours = (nowTime.QuadPart - writeTime.QuadPart) / (10000000LL * 3600LL);
    if (ageHours > LOG_MAX_DAYS * 24) {
        shouldRotate = true;
    }

    if (shouldRotate) {
        DeleteFileA(LOG_FILE_PATH.c_str());
    }
}

//Viết Log 
void WriteLog(const string& message)
{
    CreateDirectoryA(LOG_DIR_PATH.c_str(), NULL);

    HANDLE hFile = CreateFileA(
        LOG_FILE_PATH.c_str(),
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL);

    if (hFile == INVALID_HANDLE_VALUE) {
        return;
    }

    time_t t = time(nullptr);
    struct tm tmInfo;
    localtime_s(&tmInfo, &t);

    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &tmInfo);

    string line = "[" + string(timestamp) + "] " + message + "\n";
    DWORD bytesWritten = 0;
    WriteFile(hFile, line.c_str(), static_cast<DWORD>(line.size()), &bytesWritten, NULL);
    CloseHandle(hFile);
}

//-----------------------------
// Vong lap giam sat - dung chung cho ca Service mode va Console mode
//-----------------------------
void RunMonitorLoop() {
    ULONGLONG startTick = GetTickCount64();

    while (!g_ShouldStop) {
        if (WaitForSingleObject(g_ServiceStopEvent, 1000) == WAIT_OBJECT_0) {
            break;
        }

        LogRotation();
        string metrics = GetSystemMetrics();
        WriteLog(metrics);

        if (g_ConsoleMode) {
            ULONGLONG elapsedSec = (GetTickCount64() - startTick) / 1000; //Truy suất số ms trôi qua từ khi start
            unsigned int hh = static_cast<unsigned int>(elapsedSec / 3600);
            unsigned int mm = static_cast<unsigned int>((elapsedSec % 3600) / 60);
            unsigned int ss = static_cast<unsigned int>(elapsedSec % 60);

            time_t t = time(nullptr);
            struct tm tmInfo;
            localtime_s(&tmInfo, &t);
            char timestamp[16];
            strftime(timestamp, sizeof(timestamp), "%H:%M:%S", &tmInfo);

            ostringstream statusLine;
            statusLine << "[" << timestamp << "] " << metrics
                << " | Uptime: " << setfill('0')
                << setw(2) << hh << ":"
                << setw(2) << mm << ":"
                << setw(2) << ss
                << "      ";

            cout << "\r" << statusLine.str() << flush;
        }
    }
}

//-----------------------------
// Che do console: chay truc tiep, khong can SCM
//-----------------------------
void RunConsoleMode() {
    g_ConsoleMode = true;
    g_ShouldStop = false;

    SetConsoleCtrlHandler(ConsoleHandler, TRUE);

    g_ServiceStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (g_ServiceStopEvent == NULL) {
        cout << "Khong the tao stop event. Ma loi: " << GetLastError() << endl;
        return;
    }

    cout << "=== SysMonitorService - che do console ===" << endl;
    cout << "Log duoc ghi vao: " << LOG_FILE_PATH << endl;
    cout << "Nhan Ctrl+C de dung.\n" << endl;

    WriteLog("Service started (console mode).");

    RunMonitorLoop();

    WriteLog("Service stopping (console mode).");
    cout << "\n\nDa dung." << endl;

    CloseHandle(g_ServiceStopEvent);
    g_ServiceStopEvent = NULL;
}

//-----------------------------
// Cai dat Service voi chuc nang Auto-Restart
//-----------------------------
void InstallService() {
    TCHAR szPath[MAX_PATH] = { 0 };
    GetModuleFileName(NULL, szPath, MAX_PATH);

    basic_string<TCHAR> binaryPath = _T("\"");
    binaryPath += szPath;
    binaryPath += _T("\"");

    SC_HANDLE schSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (schSCManager == NULL) {
        cerr << "OpenSCManager failed. Hay chay Terminal bang quyen Administrator!" << endl;
        return;
    }

    SC_HANDLE schService = CreateService(
        schSCManager,
        SERVICE_NAME,
        SERVICE_NAME,
        SERVICE_ALL_ACCESS,
        SERVICE_WIN32_OWN_PROCESS,
        SERVICE_AUTO_START,
        SERVICE_ERROR_NORMAL,
        const_cast<LPTSTR>(binaryPath.c_str()),
        NULL,
        NULL,
        NULL,
        NULL,
        NULL
    );


    if (schService == NULL)
    {
        DWORD err = GetLastError();

        if (err == ERROR_SERVICE_EXISTS)
        {
            cout << "Service already installed." << endl;
        }
        else
        {
            cout << "CreateService failed ("
                << err
                << ")" << endl;
        }

        CloseServiceHandle(schSCManager);
        return;
    }

    cout << "CreateService thanh cong!" << endl;

    // =================================
    // Cấu hình Service Failure Actions
	// =================================

    // Resart Service tối đa 3 lần, mỗi lần cách nhau 5s
	SC_ACTION actions[3];

	actions[0].Type = SC_ACTION_RESTART;
	actions[0].Delay = 5000; // 5 giây

    actions[1].Type = SC_ACTION_RESTART;
	actions[1].Delay = 5000; // 5 giây

	actions[2].Type = SC_ACTION_RESTART;
	actions[2].Delay = 5000; // 5 giây

	SERVICE_FAILURE_ACTIONS sfa;
    ZeroMemory(&sfa, sizeof(sfa));

	sfa.dwResetPeriod = 24 * 60 * 60; // Reset sau 24h
	sfa.lpRebootMsg = NULL;
	sfa.lpCommand = NULL;
    sfa.cActions = 3;
	sfa.lpsaActions = actions;

    //Tự restart service nếu bị crack
	if (!ChangeServiceConfig2(
        schService, 
        SERVICE_CONFIG_FAILURE_ACTIONS, 
        &sfa)) 
    {
		cout << "Khong the cau hinh Failure Actions. Error:" 
            << GetLastError() << endl;
	}

	SERVICE_FAILURE_ACTIONS_FLAG flag;
	flag.fFailureActionsOnNonCrashFailures = TRUE;

	if (!ChangeServiceConfig2(
		schService,
		SERVICE_CONFIG_FAILURE_ACTIONS_FLAG,
		&flag))
	{
		cout << "Khong the cau hinh Failure Actions Flag. Error:"
			<< GetLastError() << endl;
	}

    CloseServiceHandle(schService);
    CloseServiceHandle(schSCManager);
}

//-----------------------------
// Go bo Service
//-----------------------------
void UninstallService() {
    SC_HANDLE schSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (schSCManager == NULL) {
        cerr << "OpenSCManager failed. Hay chay Terminal bang quyen Administrator!" << endl;
        return;
    }

    SC_HANDLE schService = OpenService(schSCManager, SERVICE_NAME, SERVICE_STOP | DELETE);
    if (schService == NULL) {
        DWORD err = GetLastError();
        if (err == ERROR_SERVICE_DOES_NOT_EXIST) {
            cout << "Service chua duoc cai dat." << endl;
        }
        else {
            cout << "OpenService failed (" << err << ")" << endl;
        }
        CloseServiceHandle(schSCManager);
        return;
    }

    SERVICE_STATUS status;
    ControlService(schService, SERVICE_CONTROL_STOP, &status);

    if (DeleteService(schService)) {
        cout << "Da go bo service thanh cong." << endl;
    }
    else {
        cout << "Go bo service failed (" << GetLastError() << ")" << endl;
    }

    CloseServiceHandle(schService);
    CloseServiceHandle(schSCManager);
}

void PrintUsage() {
    cout << "==============================\n";
	cout << "SysMonitorService\n";
    cout << "==============================\n";
    cout << "Usage:\n";
	cout << "  SysMonitorService install   - Cai dat service\n";
    cout << "  SysMonitorService uninstall - Go bo service\n";
	cout << "  SysMonitorService console   - Chay che do console\n";
	cout << "  SysMonitorService run       - Chay che do console\n";
	cout << "  SysMonitorService help      - Hien thi huong dan\n";
	cout << "  SysMonitorService /?        - Hien thi huong dan\n";
	cout << "  SysMonitorService           - Chay nhu mot Windows Service\n";
}