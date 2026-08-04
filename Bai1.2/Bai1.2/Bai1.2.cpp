#include <iostream>
#include <cstring>

using namespace std;


// ================= UNSAFE =================
// Dùng con trỏ
void UnsafeCopy(char* dst, const char* src)
{
    while (*src != '\0')
    {
        *dst = *src;

        dst++;
        src++;
    }

    *dst = '\0';
}


// ================= SAFE =================
// Dùng con trỏ + size
void SafeCopy(char* dst, size_t size, const char* src)
{
	if (dst == nullptr || src == nullptr || size == 0)
		return;
    
    size_t i = 0;

    while (i < size - 1 && src[i] != '\0')
    {
        dst[i] = src[i];
        i++;
    }

    dst[i] = '\0';
}


// Mô phỏng vùng nhớ liền nhau
struct Memory
{
    char buffer[8];
    char canary[8];
};


int main()
{
    Memory mem{};

    strcpy_s(mem.canary,sizeof (mem.canary), "SAFE");


    const char* data = "AAAAAAAAAAAA";


    cout << "Before:\n";
    cout << "buffer = " << mem.buffer << endl;
    cout << "canary = " << mem.canary << endl;


    cout << "\nUnsafe Copy:\n";

    UnsafeCopy(mem.buffer, data);

    cout << "buffer = " << mem.buffer << endl;
    cout << "canary = " << mem.canary << endl;



    cout << "\nSafe Copy:\n";

    Memory mem2{};

    strcpy_s(mem2.canary, sizeof(mem2.canary), "SAFE");

    SafeCopy(mem2.buffer, sizeof(mem2.buffer), data);

    cout << "buffer = " << mem2.buffer << endl;
    cout << "canary = " << mem2.canary << endl;


    return 0;
}