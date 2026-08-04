#include <windows.h>
#include <iostream>

using namespace std;

int wmain(int argc, wchar_t* argv[])
{
    wcout << L"=============================\n";
    wcout << L"Child Process Started\n";
    wcout << L"=============================\n\n";

    //--------------------------------------------------
    // PID của Child
    //--------------------------------------------------

    wcout << L"PID : "
        << GetCurrentProcessId()
        << endl;

    //--------------------------------------------------
    // Số lượng tham số
    //--------------------------------------------------

    wcout << L"Number of Arguments : "
        << argc
        << endl
        << endl;

    //--------------------------------------------------
    // Hiển thị từng tham số
    //--------------------------------------------------

    for (int i = 0; i < argc; i++)
    {
        wcout << L"argv[" << i << L"] = "
            << argv[i]
            << endl;
    }

    wcout << endl;

    //--------------------------------------------------
    // Giả lập xử lý công việc
    //--------------------------------------------------

    for (int i = 1; i <= 5; i++)
    {
        wcout << L"Working... "
            << i
            << L"/5"
            << endl;

        Sleep(1000);
    }

    //--------------------------------------------------
    // Kết thúc
    //--------------------------------------------------

    wcout << endl;
    wcout << L"Child Finished Successfully!" << endl;

    // Trả về Exit Code
    return 123;
}