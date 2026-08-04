#include <windows.h>
#include <iostream>

using namespace std;

int wmain()
{
    //--------------------------------------------------
    // Tạo file output.txt để redirect stdout
    //--------------------------------------------------

    SECURITY_ATTRIBUTES sa;

    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = NULL; //Sử dụng quyền mặc định no phải admin
    sa.bInheritHandle = TRUE;

    HANDLE hFile = CreateFileW(
        L"output.txt",
        GENERIC_WRITE,
        FILE_SHARE_READ,
        &sa, // Truyền cấu trúc của SECURITY_ATTRIBUTES
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hFile == INVALID_HANDLE_VALUE)
    {
        wcout << L"Cannot create output file!\n";
        wcout << L"Error Code = " << GetLastError() << endl;
        return 1;
    }

    //--------------------------------------------------
    // STARTUPINFO
    //--------------------------------------------------

    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));

    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hFile; // Chuyển hướng stdout của Child sang file output.txt
    si.hStdError = hFile; // Nếu child ghi lỗi thì nội dung vẫn được ghi vào file output.txt

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    //--------------------------------------------------
    // Command Line
    //--------------------------------------------------

    wchar_t cmdLine[] =
        L"Child.exe Hello Windows API";

    DWORD startTime = GetTickCount(); // Lấy thời gian bắt đầu chạy Child

    //--------------------------------------------------
    // Create Process
    //--------------------------------------------------

    BOOL success = CreateProcessW(
        NULL,
        cmdLine,
        NULL,
        NULL,
        TRUE,
        0,
        NULL,
        NULL,
        &si,
        &pi
    );

    if (!success)
    {
        wcout << L"CreateProcess Failed!\n";
        wcout << L"Error Code = " << GetLastError() << endl;

        CloseHandle(hFile);
        return 1;
    }

    //--------------------------------------------------
    // Hiển thị PID
    //--------------------------------------------------

    wcout << L"Parent PID : "
        << GetCurrentProcessId()
        << endl;

    wcout << L"Child PID  : "
        << pi.dwProcessId
        << endl;

    //--------------------------------------------------
    // Chờ Child kết thúc
    //--------------------------------------------------

    WaitForSingleObject( //Chờ process của child
        pi.hProcess,
        INFINITE);

    DWORD endTime = GetTickCount(); //Lấy số mili giây kể từ khi Windows khởi động

    //--------------------------------------------------
    // Lấy Exit Code
    //--------------------------------------------------

    DWORD exitCode = 0;

    GetExitCodeProcess( //Đọc giá trị Child trả về 
        pi.hProcess,
        &exitCode);

    //--------------------------------------------------
    // Kết quả
    //--------------------------------------------------

    wcout << endl;

    wcout << L"Run Time : "
        << (endTime - startTime)
        << L" ms"
        << endl;

    wcout << L"Exit Code : "
        << exitCode
        << endl;

    //--------------------------------------------------
    // Giải phóng Handle
    //--------------------------------------------------

    CloseHandle(hFile);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    wcout << L"\nOutput redirected to output.txt\n";

    return 0;
}