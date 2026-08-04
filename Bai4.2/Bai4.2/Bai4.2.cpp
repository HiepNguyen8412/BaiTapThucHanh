// ============================================================================
// Bai tap 4.2 - Chuong trinh duyet thu muc va tim file PE (Portable Executable)
// Ban chinh sua toi thieu tu Bai4.2(1).cpp: giu nguyen kien truc va giao dien,
// chi sua quan ly trang thai nut, kiem tra PE, dong chuong trinh va khoi tao loi.
//
// Cac API duoc nghien cuu va ap dung trong bai nay:
//   1) FindFirstFile / FindNextFile  -> duyet noi dung tung thu muc
//   2) CreateThread                  -> chay viec quet o luong nen, khong treo UI
//   3) CreateEvent                   -> co "STOP" bao cho tat ca luong dung lai
//   4) CreateMutex                   -> (a) dam bao chi 1 instance chuong trinh
//                                        (b) bao ve bien dem so luong thread dang chay
//   5) CreateSemaphore               -> gioi han so luong thread quet song song
//      (tranh viec tao hang ngan thread khi quet C:\ - moi thu muc con neu het
//      "slot" thi se duoc quet TUAN TU ngay trong thread hien tai thay vi tao
//      them thread moi)
//
// Giao dien (Win32 API thuan, khong dung .rc/MFC):
//   - Edit box nhap duong dan
//   - Nut Browse (SHBrowseForFolder)
//   - ListView (report mode) hien thi file PE tim duoc
//   - Nut Scan / Stop / Clear
// ============================================================================

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <string>
#include <cwchar>

using namespace std;

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

#ifdef _MSC_VER
#pragma comment(linker,"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif

// ---------------------------- ID cac control -------------------------------
//Windows sẽ gửi ID này về WndProc() để chương trình biết người dùng vừa thao tác với control nào
#define IDC_EDIT_PATH       1001
#define IDC_BTN_BROWSE      1002
#define IDC_LIST_RESULT     1003
#define IDC_BTN_SCAN        1004
#define IDC_BTN_STOP        1005
#define IDC_BTN_CLEAR       1006
#define IDC_STATIC_STATUS   1007

// ---------------------------- Message tu thread nen -> UI thread -----------
//thread nền không được phép cập nhật giao diện trực tiếp nên khi tìm thấy file PE hoặc quét xong sẽ gửi các message này về UI Thread bằng PostMessage()
#define WM_APP_PE_FOUND      (WM_APP + 1)
#define WM_APP_SCAN_COMPLETE (WM_APP + 2)
#define WM_APP_STATUS        (WM_APP + 3)

static const int  MAX_DEPTH = 10;   // do sau thu muc con toi da
static const LONG MAX_CONCURRENT_THREADS = 8;    // so thread CON toi da (root thread khong tinh trong Semaphore)

// Ten Mutex toan cuc dung de kiem tra single-instance. "Global\\" giup phat hien
// duoc ca khi user khac / session khac da chay (tren may thuc te can quyen phu hop).
static const wchar_t* MUTEX_NAME = L"Global\\PEScanner_Baitap4_2_SingleInstanceMutex";

// ---------------------------- Bien toan cuc ---------------------------------
static HWND g_hMainWnd = NULL;
static HWND g_hEditPath = NULL;
static HWND g_hListView = NULL;
static HWND g_hBtnScan = NULL;
static HWND g_hBtnStop = NULL;
static HWND g_hBtnClear = NULL;
static HWND g_hBtnBrowse = NULL;
static HWND g_hStatus = NULL;

static HANDLE g_hStopEvent = NULL;  // Event: bao hieu yeu cau dung quet
static HANDLE g_hCountMutex = NULL;  // Mutex: bao ve g_activeThreadCount
static HANDLE g_hThreadSemaphore = NULL;  // Semaphore: gioi han so thread quet dong thoi

static LONG g_activeThreadCount = 0;      // so thread quet dang chay (duoc bao ve boi g_hCountMutex)
static LONG g_totalScanned = 0;      // tong so file da xet qua (Interlocked, chi de hien thi)
static LONG g_totalPEFound = 0;      // tong so file PE tim thay (Interlocked, chi de hien thi)
static int  g_resultCount = 0;      // so dong da them vao ListView (chi dung tren UI thread)
static bool g_isScanning = false;  // co dang quet hay khong (chi dung tren UI thread)
static bool g_closeAfterScan = false; // true neu nguoi dung chon thoat khi dang quet

// ---------------------------- Cau truc du lieu ------------------------------
struct ScanTask {
    wstring path;
    int  depth;
    bool ownsSemaphoreSlot; // true neu thread nay da "xin" 1 slot Semaphore, phai tra lai khi xong
};

struct PEFoundInfo {
    wstring path;
    unsigned long long size; // Thông tin đường dẫn và kích thước sẽ được lưu vào rồi gửi về UI 
};

// ---------------------------- Forward declaration ---------------------------
DWORD WINAPI ThreadEntry(LPVOID lpParam);
void ScanDirectory(const wstring& dir, int depth);
bool IsPEFile(const wstring& filePath); //Kiểm tra file PE
void UpdateStatus(const wchar_t* stateText);
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
void BrowseForFolder(HWND hwnd);
void StartScan();
void StopScan();
void ResetResults();
void ClearResults();
void SetControlsIdle();
void SetControlsScanning();
void SetControlsStopping();
wstring JoinPath(const wstring& dir, const wstring& name);

static wstring g_lastState = L"San sang.";

// ============================================================================
// Cac ham tien ich cho dong bo hoa bien dem thread (dung Mutex)
// ============================================================================
void IncActiveThreads() {
    WaitForSingleObject(g_hCountMutex, INFINITE);
    g_activeThreadCount++;
    ReleaseMutex(g_hCountMutex);
}

// Tra ve true neu day la thread cuoi cung ket thuc (dem ve 0)
bool DecActiveThreadsAndCheckDone() {
    WaitForSingleObject(g_hCountMutex, INFINITE);
    g_activeThreadCount--;
    bool done = (g_activeThreadCount == 0);
    ReleaseMutex(g_hCountMutex);
    return done;
}

wstring JoinPath(const wstring& dir, const wstring& name) {
    if (!dir.empty() && dir.back() == L'\\') return dir + name;
    return dir + L"\\" + name;
}

// Quan ly trang thai cac control tai mot noi de tranh bat/tat sai nut.
void SetControlsIdle() {
    EnableWindow(g_hBtnScan, TRUE);
    EnableWindow(g_hBtnBrowse, TRUE);
    EnableWindow(g_hEditPath, TRUE);
    EnableWindow(g_hBtnStop, FALSE);
    EnableWindow(g_hBtnClear, TRUE);
}

void SetControlsScanning() {
    EnableWindow(g_hBtnScan, FALSE);
    EnableWindow(g_hBtnBrowse, FALSE);
    EnableWindow(g_hEditPath, FALSE);
    EnableWindow(g_hBtnStop, TRUE);
    EnableWindow(g_hBtnClear, TRUE); // van sang; ClearResults se bao loi khi dang quet
}

void SetControlsStopping() {
    EnableWindow(g_hBtnScan, FALSE);
    EnableWindow(g_hBtnBrowse, FALSE);
    EnableWindow(g_hEditPath, FALSE);
    EnableWindow(g_hBtnStop, FALSE);
    EnableWindow(g_hBtnClear, TRUE);
}

// ============================================================================
// Kiem tra 1 file co phai la PE hay khong bang cach doc DOS header + NT
// signature - KHONG dua vao phan mo rong, vi file PE co the mang duoi bat ky
// (.dat, .bin, .sys, khong duoi, ...)
// ============================================================================
bool IsPEFile(const wstring& filePath) {
    HANDLE hFile = CreateFileW(
        filePath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hFile == INVALID_HANDLE_VALUE) return false;

    bool result = false;
    LARGE_INTEGER fileSize = {};
    IMAGE_DOS_HEADER dosHeader = {};
    DWORD bytesRead = 0;

    // File phai du lon de chua DOS header.
    if (!GetFileSizeEx(hFile, &fileSize) ||
        fileSize.QuadPart < static_cast<LONGLONG>(sizeof(IMAGE_DOS_HEADER))) {
        CloseHandle(hFile);
        return false;
    }

    if (ReadFile(hFile, &dosHeader, sizeof(dosHeader), &bytesRead, NULL) &&
        bytesRead == sizeof(dosHeader) &&
        dosHeader.e_magic == IMAGE_DOS_SIGNATURE) { // "MZ"

        // e_lfanew phai nam trong file va con du 4 byte de doc "PE\0\0".
        const LONGLONG ntOffset = static_cast<LONGLONG>(dosHeader.e_lfanew);
        const LONGLONG maxNtOffset = fileSize.QuadPart - static_cast<LONGLONG>(sizeof(DWORD));

        if (ntOffset >= static_cast<LONGLONG>(sizeof(IMAGE_DOS_HEADER)) &&
            ntOffset <= maxNtOffset) {

            LARGE_INTEGER li = {};
            li.QuadPart = ntOffset;

            if (SetFilePointerEx(hFile, li, NULL, FILE_BEGIN)) {
                DWORD ntSignature = 0;

                if (ReadFile(hFile, &ntSignature, sizeof(ntSignature), &bytesRead, NULL) &&
                    bytesRead == sizeof(ntSignature) &&
                    ntSignature == IMAGE_NT_SIGNATURE) { // "PE\0\0"
                    result = true;
                }
            }
        }
    }

    CloseHandle(hFile);
    return result;
}

// ============================================================================
// Duyet mot thu muc bang FindFirstFile/FindNextFile.
//  - Voi file: kiem tra PE, neu dung thi PostMessage cho UI thread them vao list
//  - Voi thu muc con (con trong gioi han MAX_DEPTH):
//       + Neu Semaphore con "slot" -> tao THREAD MOI de quet song song
//       + Neu het slot             -> de quy TUAN TU ngay trong thread hien tai
//  - Truoc khi xu ly moi phan tu deu kiem tra g_hStopEvent de dap ung nut Stop
//    gan nhu ngay lap tuc.
// ============================================================================
void ScanDirectory(const wstring& dir, int depth) {
    if (WaitForSingleObject(g_hStopEvent, 0) == WAIT_OBJECT_0) return;

    wstring searchPath = JoinPath(dir, L"*");
    WIN32_FIND_DATAW findData;
    HANDLE hFind = FindFirstFileW(searchPath.c_str(), &findData);
    if (hFind == INVALID_HANDLE_VALUE) return; // khong the mo (vd: bi tu choi quyen truy cap)

    do {
        if (WaitForSingleObject(g_hStopEvent, 0) == WAIT_OBJECT_0) break;

        const wchar_t* name = findData.cFileName;
        if (wcscmp(name, L".") == 0 || wcscmp(name, L"..") == 0) continue;

        wstring fullPath = JoinPath(dir, name);

        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (depth < MAX_DEPTH) {
                DWORD waitResult = WaitForSingleObject(g_hThreadSemaphore, 0); // xin slot, khong cho
                if (waitResult == WAIT_OBJECT_0) {
                    // Con slot -> tao thread rieng de quet nhanh thu muc nay song song
                    ScanTask* task = new ScanTask{ fullPath, depth + 1, true };
                    IncActiveThreads();
                    HANDLE hThread = CreateThread(NULL, 0, ThreadEntry, task, 0, NULL);
                    if (hThread) {
                        // Dong handle khong lam thread dung.
                        // Thread van tiep tuc chay binh thuong.
                        CloseHandle(hThread);
                    }
                    else {
                        // Tao thread that bai -> tra lai slot va quet tuan tu thay the
                        ReleaseSemaphore(g_hThreadSemaphore, 1, NULL);
                        DecActiveThreadsAndCheckDone();
                        delete task;
                        ScanDirectory(fullPath, depth + 1);
                    }
                }
                else {
                    // Het slot Semaphore -> quet tuan tu ngay trong thread nay,
                    // khong tao them thread (tranh no ra qua nhieu thread khi
                    // quet ca o dia C:\)
                    ScanDirectory(fullPath, depth + 1);
                }
            }
            // depth == MAX_DEPTH: khong duyet sau hon nua (dat gioi han)
        }
        else {
            LONG scanned = InterlockedIncrement(&g_totalScanned);

            if (IsPEFile(fullPath)) {
                InterlockedIncrement(&g_totalPEFound);

                unsigned long long size =
                    (((unsigned long long)findData.nFileSizeHigh) << 32) |
                    findData.nFileSizeLow;

                PEFoundInfo* info = new PEFoundInfo{ fullPath, size };

                if (!PostMessageW(
                    g_hMainWnd,
                    WM_APP_PE_FOUND,
                    0,
                    (LPARAM)info)) {
                    delete info;
                }
            }

            // Cap nhat sau moi 512 file thay vi 64 file
            if ((scanned & 0x1FF) == 0) {
                PostMessageW(g_hMainWnd, WM_APP_STATUS, 0, 0);
            }
        }
    } while (FindNextFileW(hFind, &findData));

    FindClose(hFind);
}

// ============================================================================
// Ham thuc thi cua MOI worker thread (ca thread goc lan cac thread con)
// ============================================================================
DWORD WINAPI ThreadEntry(LPVOID lpParam) {
    ScanTask* task = (ScanTask*)lpParam;

    ScanDirectory(task->path, task->depth);

    if (task->ownsSemaphoreSlot) {
        ReleaseSemaphore(g_hThreadSemaphore, 1, NULL); // tra lai slot Semaphore neu thread nay da "xin" truoc do
    }

    if (DecActiveThreadsAndCheckDone()) {
        // Day la thread cuoi cung ket thuc -> bao UI thread la da quet xong
        PostMessageW(g_hMainWnd, WM_APP_SCAN_COMPLETE, 0, 0);
    }

    delete task;
    return 0;
}

// ============================================================================
// Cac thao tac dieu khien qua trinh quet (chay tren UI thread)
// ============================================================================
void StartScan() {
    if (g_isScanning) {
        return;

    }

    wchar_t pathBuf[MAX_PATH];
    GetWindowTextW(g_hEditPath, pathBuf, MAX_PATH);
    wstring rootPath(pathBuf);

    if (rootPath.empty()) {
        MessageBoxW(g_hMainWnd, L"Vui long nhap hoac chon duong dan thu muc can quet.",
            L"Thieu du lieu", MB_ICONWARNING | MB_OK);
        return;
    }

    DWORD attrs = GetFileAttributesW(rootPath.c_str()); //Kiểm tra thư mục 
    if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        MessageBoxW(g_hMainWnd, L"Duong dan khong ton tai hoac khong phai la thu muc.",
            L"Duong dan khong hop le", MB_ICONERROR | MB_OK);
        return;
    }

    // Bo dau '\' o cuoi (tru truong hop goc o dia nhu "C:\")
    while (rootPath.size() > 3 &&
        (rootPath.back() == L'\\' || rootPath.back() == L'/')) {
        rootPath.pop_back();
    }


    // Bat dau phien quet moi, xoa ket qua cua lan quet truoc
    ResetResults();

    ResetEvent(g_hStopEvent);
    g_closeAfterScan = false;
    g_isScanning = true;

    SetControlsScanning();
    UpdateStatus(L"Dang quet...");

    // Cap nhat hinh anh cua cac control ngay lap tuc
    RedrawWindow(
        g_hMainWnd,
        NULL,
        NULL,
        RDW_INVALIDATE |
        RDW_ALLCHILDREN |
        RDW_UPDATENOW
    );

    IncActiveThreads(); // tinh luon cho thread goc (0 -> 1)

    ScanTask* rootTask =
        new ScanTask{ rootPath, 0, false };

    HANDLE hRoot = CreateThread(
        NULL,
        0,
        ThreadEntry,
        rootTask,
        0,
        NULL);

    if (hRoot) {
        CloseHandle(hRoot); // khong join; viec theo doi hoan tat dua vao WM_APP_SCAN_COMPLETE
    }
    else {
        DecActiveThreadsAndCheckDone();
        delete rootTask;

        g_isScanning = false;

        MessageBoxW(g_hMainWnd,
            L"Khong the tao thread quet.",
            L"Loi",
            MB_ICONERROR | MB_OK
        );

        SetControlsIdle();
        UpdateStatus(L"San sang.");
    }
}

void StopScan() {
    if (!g_isScanning) return;

    // Neu Event da duoc set thi chuong trinh dang trong qua trinh dung.
    if (WaitForSingleObject(g_hStopEvent, 0) == WAIT_OBJECT_0) return;

    SetEvent(g_hStopEvent); // bao hieu cho TAT CA thread dang chay dung lai
    SetControlsStopping();
    UpdateStatus(L"Dang dung...");
}

void UpdateStatus(const wchar_t* stateText) {
    if (stateText) g_lastState = stateText;
    wchar_t buf[256];
    swprintf(buf, 256, L"%s   |   Da xet: %ld file   |   Tim thay PE: %ld",
        g_lastState.c_str(), g_totalScanned, g_totalPEFound);
    SetWindowTextW(g_hStatus, buf);
}

// Xoa du lieu ket qua, dung noi bo trong chuong trinh
void ResetResults() {
    SendMessageW(g_hListView, WM_SETREDRAW, FALSE, 0);

    ListView_DeleteAllItems(g_hListView);

    g_resultCount = 0;
    g_totalScanned = 0;
    g_totalPEFound = 0;

    SendMessageW(g_hListView, WM_SETREDRAW, TRUE, 0);

    RedrawWindow(
        g_hListView,
        NULL,
        NULL,
        RDW_INVALIDATE |
        RDW_ERASE |
        RDW_UPDATENOW
    );
}

// Xu ly khi nguoi dung nhan nut Clear
void ClearResults() {
    if (g_isScanning) {
        MessageBoxW(
            g_hMainWnd,
            L"Chuong trinh dang quet, khong the xoa ket qua luc nay.",
            L"Khong the Clear",
            MB_OK | MB_ICONWARNING
        );
        return;
    }

    ResetResults();
    UpdateStatus(L"San sang.");
}

// ============================================================================
// Chon thu muc bang hop thoai Browse (SHBrowseForFolder)
// ============================================================================
void BrowseForFolder(HWND hwnd) {
    if (g_isScanning) {
        MessageBoxW(
            hwnd,
            L"Chuong trinh dang quet hoac dang dung. Vui long cho qua trinh ket thuc.",
            L"Khong the Browse",
            MB_OK | MB_ICONWARNING
        );
        return;
    }

    wchar_t displayName[MAX_PATH] = { 0 };
    BROWSEINFOW bi = {};
    bi.hwndOwner = hwnd;
    bi.pszDisplayName = displayName;
    bi.lpszTitle = L"Chon thu muc can quet";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (pidl) {
        wchar_t path[MAX_PATH];
        if (SHGetPathFromIDListW(pidl, path)) {
            SetWindowTextW(g_hEditPath, path);
        }
        CoTaskMemFree(pidl);
    }
}

// ============================================================================
// Thu tuc cua so chinh
// Giao diện của Chương trình
// ============================================================================
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        CreateWindowExW(0, L"STATIC", L"Thu muc:", WS_CHILD | WS_VISIBLE,
            10, 14, 60, 20, hwnd, NULL, NULL, NULL);

        g_hEditPath = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            75, 10, 500, 24, hwnd, (HMENU)IDC_EDIT_PATH, NULL, NULL);

        g_hBtnBrowse = CreateWindowExW(0, L"BUTTON", L"Browse...",
            WS_CHILD | WS_VISIBLE,
            585, 10, 90, 24, hwnd, (HMENU)IDC_BTN_BROWSE, NULL, NULL);

        g_hBtnScan = CreateWindowExW(
            0,
            L"BUTTON",
            L"Scan",
            WS_CHILD | WS_VISIBLE,
            10, 44, 90, 28,
            hwnd,
            (HMENU)IDC_BTN_SCAN,
            NULL,
            NULL
        );

        g_hBtnStop = CreateWindowExW(
            0,
            L"BUTTON",
            L"Stop",
            WS_CHILD | WS_VISIBLE | WS_DISABLED,
            108, 44, 90, 28,
            hwnd,
            (HMENU)IDC_BTN_STOP,
            NULL,
            NULL
        );

        g_hBtnClear = CreateWindowExW(
            0,
            L"BUTTON",
            L"Clear",
            WS_CHILD | WS_VISIBLE,
            206, 44, 90, 28,
            hwnd,
            (HMENU)IDC_BTN_CLEAR,
            NULL,
            NULL
        );

        g_hListView = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            WC_LISTVIEWW,
            L"",
            WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS,
            10, 82, 665, 360,
            hwnd,
            (HMENU)IDC_LIST_RESULT,
            NULL,
            NULL
        );

        if (g_hListView == NULL) {
            MessageBoxW(
                hwnd,
                L"Khong the tao ListView.",
                L"Loi giao dien",
                MB_OK | MB_ICONERROR
            );
        }

        ListView_SetExtendedListViewStyle(g_hListView, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

        LVCOLUMNW col = {};
        col.mask = LVCF_TEXT | LVCF_WIDTH;
        col.cx = 550;
        col.pszText = (LPWSTR)L"Duong dan file PE";
        ListView_InsertColumn(g_hListView, 0, &col);

        col.cx = 110;
        col.pszText = (LPWSTR)L"Kich thuoc (bytes)";
        ListView_InsertColumn(g_hListView, 1, &col);

        g_hStatus = CreateWindowExW(0, L"STATIC", L"San sang.",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            10, 452, 665, 20, hwnd, (HMENU)IDC_STATIC_STATUS, NULL, NULL);

        HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        HWND child = GetWindow(hwnd, GW_CHILD);
        while (child) {
            SendMessageW(child, WM_SETFONT, (WPARAM)hFont, TRUE);
            child = GetWindow(child, GW_HWNDNEXT);
        }
        return 0;
    }

    case WM_SIZE: {
        int w = LOWORD(lParam);
        int h = HIWORD(lParam);
        if (g_hEditPath) {
            MoveWindow(g_hEditPath, 75, 10, w - 75 - 10 - 100, 24, TRUE);
            MoveWindow(g_hBtnBrowse, w - 100, 10, 90, 24, TRUE);
            MoveWindow(g_hListView, 10, 82, w - 20, h - 82 - 30, TRUE);
            MoveWindow(g_hStatus, 10, h - 24, w - 20, 20, TRUE);
        }
        return 0;
    }

    case WM_COMMAND:
        if (HIWORD(wParam) == BN_CLICKED) {
            switch (LOWORD(wParam)) {
            case IDC_BTN_BROWSE: BrowseForFolder(hwnd); break;
            case IDC_BTN_SCAN:   StartScan(); break;
            case IDC_BTN_STOP:   StopScan(); break;
            case IDC_BTN_CLEAR:  ClearResults(); break;
            }
        }
        return 0;

    case WM_APP_PE_FOUND: {
        PEFoundInfo* info = (PEFoundInfo*)lParam;

        LVITEMW item = {};
        item.mask = LVIF_TEXT;
        item.iItem = g_resultCount;
        item.iSubItem = 0;
        item.pszText = (LPWSTR)info->path.c_str();

        int idx = ListView_InsertItem(g_hListView, &item);

        if (idx != -1) {
            wchar_t sizeBuf[64];
            swprintf(sizeBuf, 64, L"%llu", info->size);
            ListView_SetItemText(g_hListView, idx, 1, sizeBuf);

            g_resultCount++;
        }

        delete info;

        // Khong UpdateStatus tai day
        return 0;
    }

    case WM_APP_STATUS:
        UpdateStatus(NULL);
        return 0;

    case WM_APP_SCAN_COMPLETE: {
        g_isScanning = false;

        bool wasStopped =
            (WaitForSingleObject(g_hStopEvent, 0) == WAIT_OBJECT_0);

        if (g_closeAfterScan) {
            DestroyWindow(hwnd);
            return 0;
        }

        UpdateStatus(wasStopped ? L"Da dung quet." : L"Hoan tat quet.");
        SetControlsIdle();

        RedrawWindow(
            g_hMainWnd,
            NULL,
            NULL,
            RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW
        );
        return 0;
    }

    case WM_CLOSE:
        if (g_isScanning) {
            int r = MessageBoxW(
                hwnd,
                L"Chuong trinh dang quet du lieu. Ban co chac muon thoat?",
                L"Xac nhan thoat",
                MB_YESNO | MB_ICONQUESTION
            );

            if (r != IDYES) return 0;

            // Khong huy cua so ngay. Cho thread cuoi cung ket thuc roi moi thoat.
            g_closeAfterScan = true;
            SetEvent(g_hStopEvent);
            SetControlsStopping();
            UpdateStatus(L"Dang dung de thoat...");
            return 0;
        }

        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ============================================================================
// Entry point
// ============================================================================
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance; (void)lpCmdLine;

    // ---- Kiem tra chuong trinh da chay hay chua, dung CreateMutex co ten ----
    HANDLE hSingleInstance = CreateMutexW(NULL, TRUE, MUTEX_NAME);
    DWORD mutexErr = GetLastError();

    if (hSingleInstance == NULL) {
        MessageBoxW(NULL,
            L"Khong the tao mutex kiem tra chuong trinh.",
            L"Loi khoi tao", MB_ICONERROR | MB_OK);
        return 1;
    }

    if (mutexErr == ERROR_ALREADY_EXISTS) {
        MessageBoxW(NULL,
            L"Chuong trinh dang chay roi. Khong the mo them mot phien ban nua.",
            L"PE Scanner - Bai tap 4.2", MB_ICONWARNING | MB_OK);
        if (hSingleInstance) CloseHandle(hSingleInstance);
        return 0;
    }

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    // ---- Tao cac doi tuong dong bo hoa ----
    g_hStopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);   // manual-reset, ban dau chua bao hieu
    g_hCountMutex = CreateMutexW(NULL, FALSE, NULL);        // mutex khong ten, chua ai giu
    g_hThreadSemaphore = CreateSemaphoreW(NULL, MAX_CONCURRENT_THREADS, MAX_CONCURRENT_THREADS, NULL);

    if (!g_hStopEvent || !g_hCountMutex || !g_hThreadSemaphore) {
        MessageBoxW(NULL,
            L"Khong the tao cac doi tuong dong bo hoa.",
            L"Loi khoi tao", MB_ICONERROR | MB_OK);

        if (g_hStopEvent) CloseHandle(g_hStopEvent);
        if (g_hCountMutex) CloseHandle(g_hCountMutex);
        if (g_hThreadSemaphore) CloseHandle(g_hThreadSemaphore);
        CloseHandle(hSingleInstance);
        CoUninitialize();
        return 1;
    }

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"PEScannerMainWnd_BT4_2";
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    RegisterClassExW(&wc);

    g_hMainWnd = CreateWindowExW(0, wc.lpszClassName, L"Bai tap 4.2 - Quet thu muc tim file PE",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 720, 540,
        NULL, NULL, hInstance, NULL);

    if (!g_hMainWnd) {
        MessageBoxW(NULL,
            L"Khong the tao cua so chinh.",
            L"Loi giao dien", MB_ICONERROR | MB_OK);
        CloseHandle(g_hStopEvent);
        CloseHandle(g_hCountMutex);
        CloseHandle(g_hThreadSemaphore);
        CloseHandle(hSingleInstance);
        CoUninitialize();
        return 1;
    }

    ShowWindow(g_hMainWnd, nCmdShow);
    UpdateWindow(g_hMainWnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    CloseHandle(g_hStopEvent);
    CloseHandle(g_hCountMutex);
    CloseHandle(g_hThreadSemaphore);
    CloseHandle(hSingleInstance);
    CoUninitialize();

    return (int)msg.wParam;
}