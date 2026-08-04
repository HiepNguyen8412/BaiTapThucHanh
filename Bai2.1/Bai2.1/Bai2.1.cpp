#include <windows.h> 
#include <tlhelp32.h> //Tool Help Library
#include <psapi.h> // Process Status API
#include <iostream>
#include <string>

#pragma comment(lib, "Psapi.lib")

using namespace std;

//=====================================
// Lấy đường dẫn process
//=====================================
wstring GetProcessPath(DWORD pid) //Đường dẫn tuyệt đối tới file .exe (PID là ID của processes đó )
{
    wstring path = L"Access Denied";

    HANDLE hProcess = OpenProcess(
        PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, // Dùng | để gộp quyền hạn lại với nhau giúp xin nhiều quyền cùng 1 lúc
        FALSE,
        pid
    );

    if (hProcess)
    {
        wchar_t buffer[MAX_PATH];

        if (GetModuleFileNameExW(
            hProcess, // Thẻ thông hành qua Handle
            NULL, // Mục tiêu lấy đường dẫn, chỉ nhận file exe
            buffer, // Vùng lưu trữ tạm thời đã tạo 
            MAX_PATH))  // Giới hạn
        {
            path = buffer;
        }

        CloseHandle(hProcess); //Hủy tránh bị Handle Leak
    }

    return path;
}

//=====================================
// Lấy RAM process
//=====================================
SIZE_T GetProcessMemory(DWORD pid)
{
    SIZE_T memory = 0;

    HANDLE hProcess = OpenProcess(
        PROCESS_QUERY_INFORMATION |
        PROCESS_VM_READ,
        FALSE,
        pid
    );

    if (hProcess)
    {
        PROCESS_MEMORY_COUNTERS pmc;

        // Gọi API 
        if (GetProcessMemoryInfo(
            hProcess,
            &pmc, // // Truyền địa chỉ của cấu trúc pmc để nhận dữ liệu
            sizeof(pmc))) // Truyền kích thước của cấu trúc pmc để bảo vệ bộ nhớ
        {
            memory = pmc.WorkingSetSize; // Lấy dung lượng RAM đang sử dụng của tiến trình
        }

        CloseHandle(hProcess);
    }

    return memory;
}

//=====================================
// Liệt kê process
//=====================================
void ListProcesses(wstring filter = L"") // Liệt kê tất cả process đang chạy, có thể lọc theo tên nếu truyền filter
{
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

    if (snapshot == INVALID_HANDLE_VALUE)
    {
        wcout << L"Cannot create snapshot\n";
        return;
    }

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(PROCESSENTRY32W);

    if (Process32FirstW(snapshot, &pe))
    {
        do
        {
            wstring name = pe.szExeFile;

            // Filter
            if (filter != L"")
            {
                if (name.find(filter) == wstring::npos)
                    continue;
            }

            DWORD pid = pe.th32ProcessID;

            wcout << L"\n============================\n";
            wcout << L"PID: " << pid << endl;
            wcout << L"Name: " << name << endl;
            wcout << L"Path: " << GetProcessPath(pid) << endl;
            wcout << L"RAM: " << GetProcessMemory(pid) /1024 << L" KB" << endl;

        } while (Process32NextW(snapshot, &pe));
    }

    CloseHandle(snapshot);
}

//=====================================
// Kill process theo PID
//=====================================
void KillProcess(DWORD pid)
{
    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pid);

    if (hProcess == NULL)
    {
        wcout << L"Cannot open process (PID: " << pid << L")\n";
        return;
    }

    if (TerminateProcess(hProcess, 0))
    {
        wcout << L"Process " << pid << L" killed!\n";
    }
    else
    {
        wcout << L"Kill failed for PID: " << pid << L"\n";
    }

    CloseHandle(hProcess);
}

//=====================================
// Kill process theo Tên (Hàm mới)
//=====================================
void KillProcessByName(wstring processName)
{
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
    {
        wcout << L"Cannot create snapshot\n";
        return;
    }

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(PROCESSENTRY32W);
    bool isFound = false;

    if (Process32FirstW(snapshot, &pe))
    {
        do
        {
            wstring currentName = pe.szExeFile;

            // Kiểm tra xem tên tiến trình có chứa từ khóa không
            if (currentName.find(processName) != wstring::npos)
            {
                isFound = true;
                DWORD pid = pe.th32ProcessID;

                wcout << L"Found " << currentName << L" (PID: " << pid << L") -> ";
                KillProcess(pid); // Gọi hàm Kill bằng PID
            }

        } while (Process32NextW(snapshot, &pe));
    }

    if (!isFound)
    {
        wcout << L"Could not find any process matching: " << processName << endl;
    }

    CloseHandle(snapshot);
}

//=====================================
// MAIN
//=====================================
int main()

{
    while (true)
    {
        wcout << L"\n===== PROCESS EXPLORER MINI =====\n";
        wcout << L"1. List processes\n";
        wcout << L"2. Search process\n";
        wcout << L"3. Kill process (By PID)\n";
        wcout << L"4. Kill process (By Name - vd: chrome)\n"; // Đã thêm lựa chọn 4
        wcout << L"0. Exit\n";
        wcout << L"Choose: ";

        int choice;
        wcin >> choice;

        // Bắt lỗi nhập sai Menu (nhập chữ thay vì số)
        if (wcin.fail())
        {
            wcin.clear();
            wcin.ignore(10000, L'\n');
            wcout << L"[!] Ban phai nhap bang so! Vui long nhap lai.\n";
            continue;
        }
        wcin.ignore(10000, L'\n'); // Xóa bộ đệm đúng cách 1 lần duy nhất

        switch (choice)
        {
        case 1:
            ListProcesses();
            break;

        case 2:
        {
            wstring name;
            wcout << L"Enter process name: ";
            getline(wcin, name);
            ListProcesses(name);
            break;
        }

        case 3:
        {
            DWORD pid;
            wcout << L"Enter PID: ";
            wcin >> pid;

            // Bắt lỗi nhập sai PID
            if (wcin.fail())
            {
                wcin.clear();
                wcin.ignore(10000, L'\n');
                wcout << L"[!] PID phai la mot con so!\n";
                break;
            }
            wcin.ignore(10000, L'\n');

            KillProcess(pid);
            break;
        }

        case 4: // Case mới để Kill hàng loạt theo tên
        {
            wstring targetName;
            wcout << L"Enter process name to kill (e.g., chrome): ";
            getline(wcin, targetName);

            KillProcessByName(targetName);
            break;
        }

        case 0:
            return 0;

        default:
            wcout << L"Invalid choice\n";
        }
    }

    return 0;
}