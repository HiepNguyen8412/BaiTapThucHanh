#define NOMINMAX

#include <windows.h>
#include <winsvc.h>

#include <iostream>
#include <vector>
#include <string>
#include <limits> // Xóa toàn bộ dữ liệu trong bộ đệm của CIN

#pragma comment(lib, "Advapi32.lib")

using namespace std;

//====================================================
// Chuyển UTF-16 (Windows) -> UTF-8 (Console)
//====================================================
string WideToUTF8(const wstring& text)
{
    if (text.empty())
        return "";

    int sizeNeeded = WideCharToMultiByte(
        CP_UTF8,
        0,
        text.c_str(),
        (int)text.size(),
        nullptr,
        0,
        nullptr,
        nullptr
    );

    string result(sizeNeeded, 0);

    WideCharToMultiByte(
        CP_UTF8,
        0,
        text.c_str(),
        (int)text.size(),
        result.data(),
        sizeNeeded,
        nullptr,
        nullptr
    );

    return result;
}

//====================================================
// Lấy tên lỗi Windows
//====================================================
string GetErrorMessage(DWORD errorCode)
{
    LPVOID msgBuffer = nullptr;

    DWORD size = FormatMessageA( //Lấy thông báo lỗi
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        errorCode, // Mã lỗi 
        MAKELANGID(
            LANG_NEUTRAL,
            SUBLANG_DEFAULT),
        (LPSTR)&msgBuffer,
        0,
        nullptr
    );

    string message;

    if (size && msgBuffer)
    {
        message.assign(
            (LPSTR)msgBuffer,
            size
        );

		LocalFree(msgBuffer); //Giải phóng bộ nhớ
    }
    else
    {
        message = "Unknown error";
    }

    return message;
}

//====================================================
// In trạng thái Service
//====================================================
string GetServiceStatus(DWORD status)
{
    switch (status)
    {
    case SERVICE_RUNNING:
        return "RUNNING";

    case SERVICE_STOPPED:
        return "STOPPED";

    case SERVICE_START_PENDING:
        return "START_PENDING";

    case SERVICE_STOP_PENDING:
        return "STOP_PENDING";

    case SERVICE_PAUSED:
        return "PAUSED";

    default:
        return "UNKNOWN";
    }
}

//====================================================
// Liệt kê Service
//====================================================
void LietKeService()
{
    SC_HANDLE hSCM = //Lưu HLE của Service Control Manager
        OpenSCManagerW(
            nullptr,
            nullptr,
            SC_MANAGER_ENUMERATE_SERVICE
        );

    if (!hSCM)
    {
        cout << "Khong mo duoc Service Manager!\n";
        return;
    }

    DWORD bytesNeeded = 0;
    DWORD serviceCount = 0;
    DWORD resumeHandle = 0;

	EnumServicesStatusExW( // Lấy số lượng Service
        hSCM,
        SC_ENUM_PROCESS_INFO,
        SERVICE_WIN32,
        SERVICE_STATE_ALL,
		nullptr, // Không cần dữ liệu, chỉ tính toán
        0,
        &bytesNeeded,
        &serviceCount,
        &resumeHandle,
        nullptr
    );

    vector<BYTE> buffer(bytesNeeded);

    if (!EnumServicesStatusExW(
        hSCM,
        SC_ENUM_PROCESS_INFO,
        SERVICE_WIN32,
        SERVICE_STATE_ALL,
        buffer.data(),
        bytesNeeded,
        &bytesNeeded,
        &serviceCount,
        &resumeHandle,
        nullptr))
    {
        cout
            << "Loi liet ke Service: "
            << GetErrorMessage(GetLastError());

        CloseServiceHandle(hSCM);
        return;
    }

    auto services =
        reinterpret_cast<
        LPENUM_SERVICE_STATUS_PROCESSW>
        (buffer.data());

    cout << "\n";

    for (DWORD i = 0; i < serviceCount; i++)
    {
        cout
            << "==============================================================\n";

        cout
            << "Service Name      : "
            << WideToUTF8(
                services[i].lpServiceName)
            << "\n";

        cout
            << "Display Name      : "
            << WideToUTF8(
                services[i].lpDisplayName)
            << "\n";

        cout
            << "Status            : "
            << GetServiceStatus(
                services[i]
                .ServiceStatusProcess
                .dwCurrentState)
            << "\n";

        cout
            << "PID               : "
            << services[i]
            .ServiceStatusProcess
            .dwProcessId
            << "\n";

        cout
            << "--------------------------------------------------------------\n\n";
    }

    CloseServiceHandle(hSCM);
}
//====================================================
// Khởi động Service
//====================================================
void KhoiDongService()
{
    string tenServiceUTF8;

    cout << "Nhap ten Service: ";
    getline(cin, tenServiceUTF8);

    // UTF-8 -> UTF-16
    int sizeNeeded =
        MultiByteToWideChar(
            CP_UTF8,
            0,
            tenServiceUTF8.c_str(),
            -1,
            nullptr,
            0);

    if (sizeNeeded <= 0)
    {
        cout << "Loi chuyen doi ten Service!\n";
        return;
    }

    // Trừ 1 bỏ ký tự '\0' cuối
    wstring tenService(sizeNeeded - 1, L'\0');

    MultiByteToWideChar(
        CP_UTF8,
        0,
        tenServiceUTF8.c_str(),
        -1,
        tenService.data(),
        sizeNeeded);

    // Debug kiểm tra tên thực sự gửi cho Windows
    cout << "Dang tim Service: "
        << tenServiceUTF8
        << "\n";

    SC_HANDLE hSCM =
        OpenSCManagerW(
            nullptr,
            nullptr,
            SC_MANAGER_CONNECT);

    if (!hSCM)
    {
        cout << "Khong mo duoc Service Manager!\n";
        return;
    }

    SC_HANDLE hService =
        OpenServiceW(
            hSCM,
            tenService.c_str(),
            SERVICE_START |
            SERVICE_QUERY_STATUS);

    if (!hService)
    {
        DWORD error = GetLastError();

        cout << "Khong tim thay Service!\n";
        cout << "Ma loi: "
            << error
            << "\n";

        cout << GetErrorMessage(error);

        CloseServiceHandle(hSCM);
        return;
    }

    if (StartServiceW(
        hService,
        0,
        nullptr))
    {
        cout << "Khoi dong Service thanh cong!\n";
    }
    else
    {
        DWORD error = GetLastError();

        if (error == ERROR_SERVICE_ALREADY_RUNNING)
        {
            cout << "Service dang chay.\n";
        }
        else
        {
            cout << "Khoi dong that bai!\n";
            cout << "Ma loi: "
                << error
                << "\n";
            cout << GetErrorMessage(error);
        }
    }

    CloseServiceHandle(hService);
    CloseServiceHandle(hSCM);
}

//====================================================
// Dừng Service
//====================================================
void DungService()
{
    string tenServiceUTF8;

    cout << "Nhap ten Service: ";
    getline(cin, tenServiceUTF8);

    int sizeNeeded =
        MultiByteToWideChar(
            CP_UTF8,
            0,
            tenServiceUTF8.c_str(),
            -1,
            nullptr,
            0);

    wstring tenService(sizeNeeded, L'\0');

    MultiByteToWideChar(
        CP_UTF8,
        0,
        tenServiceUTF8.c_str(),
        -1,
        tenService.data(),
        sizeNeeded);

    SC_HANDLE hSCM =
        OpenSCManagerW(
            nullptr,
            nullptr,
            SC_MANAGER_CONNECT);

    if (!hSCM)
    {
        cout
            << "Khong mo duoc Service Manager!\n";
        return;
    }

    SC_HANDLE hService =
        OpenServiceW(
            hSCM,
            tenService.c_str(),
            SERVICE_STOP |
            SERVICE_QUERY_STATUS);

    if (!hService)
    {
        cout
            << "Khong tim thay Service!\n";

        CloseServiceHandle(hSCM);
        return;
    }

    SERVICE_STATUS status;

    if (ControlService(
        hService,
        SERVICE_CONTROL_STOP,
        &status))
    {
        cout
            << "Dung Service thanh cong!\n";
    }
    else
    {
        DWORD error = GetLastError();

        if (error == ERROR_SERVICE_NOT_ACTIVE)
        {
            cout
                << "Service da dung.\n";
        }
        else
        {
            cout
                << "Dung that bai: "
                << GetErrorMessage(error);
        }
    }

    CloseServiceHandle(hService);
    CloseServiceHandle(hSCM);
}

void LietKeServiceDieuKhienDuoc()
{
    SC_HANDLE hSCM =
        OpenSCManagerW(
            nullptr,
            nullptr,
            SC_MANAGER_ENUMERATE_SERVICE);

    if (!hSCM)
    {
        cout << "Khong mo duoc Service Manager\n";
        return;
    }

    DWORD bytesNeeded = 0;
    DWORD serviceCount = 0;
    DWORD resumeHandle = 0;

    EnumServicesStatusExW(
        hSCM,
        SC_ENUM_PROCESS_INFO,
        SERVICE_WIN32,
        SERVICE_STATE_ALL,
        nullptr,
        0,
        &bytesNeeded,
        &serviceCount,
        &resumeHandle,
        nullptr);

    vector<BYTE> buffer(bytesNeeded);

    if (!EnumServicesStatusExW(
        hSCM,
        SC_ENUM_PROCESS_INFO,
        SERVICE_WIN32,
        SERVICE_STATE_ALL,
        buffer.data(),
        bytesNeeded,
        &bytesNeeded,
        &serviceCount,
        &resumeHandle,
        nullptr))
    {
        cout << "Khong lay duoc danh sach Service\n";

        CloseServiceHandle(hSCM);
        return;
    }

    auto services =
        reinterpret_cast<
        LPENUM_SERVICE_STATUS_PROCESSW>
        (buffer.data());

    cout<< "\n================ SERVICE CO THE DIEU KHIEN ================\n\n";

    for (DWORD i = 0; i < serviceCount; i++)
    {

        bool canStart = false;
        bool canStop = false;

        SC_HANDLE hStart =
            OpenServiceW(
                hSCM,
                services[i].lpServiceName,
                SERVICE_START);

        if (hStart)
        {
            canStart = true;
            CloseServiceHandle(hStart);
        }

        SC_HANDLE hStop =
            OpenServiceW(
                hSCM,
                services[i].lpServiceName,
                SERVICE_STOP);

        if (hStop)
        {
            canStop = true;
            CloseServiceHandle(hStop);
        }

        if (canStart || canStop)
        {

            cout
                << "============================================================\n";

            cout
                << "Service Name : "
                << WideToUTF8(
                    services[i].lpServiceName)
                << "\n";

            cout
                << "Display Name : "
                << WideToUTF8(
                    services[i].lpDisplayName)
                << "\n";

            cout
                << "Status       : "
                << GetServiceStatus(
                    services[i]
                    .ServiceStatusProcess
                    .dwCurrentState)
                << "\n";

            cout
                << "Can Start    : "
                << (canStart ? "YES" : "NO")
                << "\n";

            cout
                << "Can Stop     : "
                << (canStop ? "YES" : "NO")
                << "\n";

            cout
                << "PID          : "
                << services[i]
                .ServiceStatusProcess
                .dwProcessId
                << "\n";
        }
    }

    CloseServiceHandle(hSCM);
}

//====================================================
// MAIN
//====================================================
int main()
{
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    int luaChon;

    while (true)
    {
        cout << "\n================ SERVICE CONTROLLER ================\n\n";

        cout << "1. Liệt kê tất cả Windows Service\n";
        cout << "2. Liệt kê Service có thể điều khiển\n";
        cout << "3. Khởi động Service\n";
        cout << "4. Dừng Service\n";
        cout << "0. Thoát\n\n";

        cout << "Lựa chọn: ";

        cin >> luaChon;

        // Kiểm tra nhập sai kiểu dữ liệu
        if (cin.fail())
        {
            cin.clear();

            cin.ignore(
                numeric_limits<streamsize>::max(),
                '\n'
            );

            cout << "\nLựa chọn không hợp lệ! Vui lòng nhập đúng.\n";

            continue;
        }

        cin.ignore(
            numeric_limits<streamsize>::max(),
            '\n'
        );

        switch (luaChon)
        {
        case 1:
            LietKeService();
            break;

        case 2:
            LietKeServiceDieuKhienDuoc();
            break;

        case 3:
            KhoiDongService();
            break;

        case 4:
            DungService();
            break;

        case 0:
            cout << "\nĐã thoát chương trình.\n";
            return 0;

        default:

            cout
                << "\nLựa chọn không hợp lệ! "
                << "Vui lòng chọn lại.\n";

            break;
        }
    }
}
