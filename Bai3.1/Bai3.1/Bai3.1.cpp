#include <windows.h>
#include <iostream>
#include <string>

using namespace std;

void AddOrEditValue()
{
	HKEY hKey; // Handle của registry key

    LONG result = RegCreateKeyExW(
        HKEY_CURRENT_USER,
        L"Software\\MyApp",
		0, // Reserved
        NULL, //Class
        0, //Options
        KEY_ALL_ACCESS,
		NULL, //Security attributes
        &hKey,
		NULL // Disposition(Tạo mới or mở sẵn)
    );

    if (result != ERROR_SUCCESS)
    {
        wcout << L"Cannot open registry!\n";
        return;
    }

    wstring value;

    wcout << L"Enter value: ";
    getline(wcin, value);

    result = RegSetValueExW(
        hKey,
        L"UserName",
        0,
        REG_SZ,
        (BYTE*)value.c_str(),
        (value.size() + 1) * sizeof(wchar_t)
    );

    if (result == ERROR_SUCCESS)
        wcout << L"Saved!\n";
    else
        wcout << L"Save failed!\n";

    RegCloseKey(hKey);
}

void DeleteValue()
{
    HKEY hKey;

    LONG result = RegOpenKeyExW(
        HKEY_CURRENT_USER,
        L"Software\\MyApp",
        0,
        KEY_SET_VALUE,
        &hKey
    );

    if (result != ERROR_SUCCESS)
    {
        wcout << L"Cannot open registry!\n";
        return;
    }

    result = RegDeleteValueW(
        hKey,
        L"UserName"
    );

    if (result == ERROR_SUCCESS)
        wcout << L"Deleted!\n";
    else
        wcout << L"Delete failed!\n";

    RegCloseKey(hKey);
}

int wmain()
{
    int choice;

    do
    {
        wcout << L"\n===== Registry CLI =====\n";
        wcout << L"1. Add/Edit Value\n";
        wcout << L"2. Delete Value\n";
        wcout << L"0. Exit\n";
        wcout << L"Choice: ";

        wcin >> choice;
        wcin.ignore();

        switch (choice)
        {
        case 1:
            AddOrEditValue();
            break;

        case 2:
            DeleteValue();
            break;
        }

    } while (choice != 0);

    return 0;
}