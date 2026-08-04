#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <iomanip>
#include <vector>
#include <thread>
#include <chrono>

using namespace std;

// Chuyển FILETIME sang số 64-bit
ULONGLONG FileTimeToUInt64(const FILETIME& ft)
{
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    return uli.QuadPart;
}

// Cấu trúc lưu thông tin Thread để tính delta
struct ThreadInfo {
    DWORD threadID;
    ULONGLONG prevKernelTime;
    ULONGLONG prevUserTime;
};

void ListThreads(DWORD pid)
{
    // Lấy mẫu lần 1
    vector<ThreadInfo> threadList;
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);

    if (hSnapshot == INVALID_HANDLE_VALUE) {
        cout << "Cannot create snapshot!" << endl;
        return;
    }

    THREADENTRY32 te;
    te.dwSize = sizeof(THREADENTRY32);

    if (Thread32First(hSnapshot, &te)) {
        do {
            if (te.th32OwnerProcessID == pid) {
                HANDLE hThread = OpenThread(THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
                if (hThread) {
                    FILETIME creation, exit, kernel, user;
                    if (GetThreadTimes(hThread, &creation, &exit, &kernel, &user)) {
                        threadList.push_back({
                            te.th32ThreadID,
                            FileTimeToUInt64(kernel),
                            FileTimeToUInt64(user)
                            });
                    }
                    CloseHandle(hThread);
                }
            }
        } while (Thread32Next(hSnapshot, &te));
    }
    CloseHandle(hSnapshot);

    if (threadList.empty()) {
        cout << "No threads found for PID " << pid << endl;
        return;
    }

    // Chờ một khoảng thời gian để tính delta (ví dụ 1 giây)
    cout << "Sampling CPU usage for 1 second...\n";
    this_thread::sleep_for(chrono::seconds(1));

    // Lấy mẫu lần 2 và tính CPU Usage
    cout << left
        << setw(10) << "TID"
        << setw(12) << "Priority"
        << setw(15) << "CPU Usage(%)"
        << setw(20) << "Status"
        << endl;
    cout << "--------------------------------------------------------------" << endl;

    hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) return;

    if (Thread32First(hSnapshot, &te)) {
        do {
            if (te.th32OwnerProcessID == pid) {
                HANDLE hThread = OpenThread(THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
                string status = "Access Denied";
                double cpuUsage = 0.0;

                if (hThread) {
                    status = "Accessible";
                    FILETIME creation, exit, kernel, user;

                    if (GetThreadTimes(hThread, &creation, &exit, &kernel, &user)) {
                        ULONGLONG kernelTime = FileTimeToUInt64(kernel);
                        ULONGLONG userTime = FileTimeToUInt64(user);

                        // Tìm thread trong list lần 1
                        for (auto& t : threadList) {
                            if (t.threadID == te.th32ThreadID) {
                                ULONGLONG deltaKernel = kernelTime - t.prevKernelTime;
                                ULONGLONG deltaUser = userTime - t.prevUserTime;
                                ULONGLONG deltaTotal = deltaKernel + deltaUser;

                                // Tính % (trong 1 giây = 10.000.000 * 100ns)
                                cpuUsage = (deltaTotal / 100000.0); // x100 để ra %
                                if (cpuUsage > 100.0) cpuUsage = 100.0;
                                break;
                            }
                        }
                    }
                    CloseHandle(hThread);
                }

                cout << left
                    << setw(10) << te.th32ThreadID
                    << setw(12) << te.tpBasePri
                    << setw(15) << fixed << setprecision(2) << cpuUsage
                    << setw(20) << status
                    << endl;
            }
        } while (Thread32Next(hSnapshot, &te));
    }
    CloseHandle(hSnapshot);
}

int main()
{
    DWORD pid;
    cout << "Enter PID: ";
    cin >> pid;

    ListThreads(pid);

    return 0;
}