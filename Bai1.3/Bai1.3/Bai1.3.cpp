#include <iostream>
#include <fstream>
#include <cstring>

#pragma pack(push, 1)

using namespace std;

struct FILE_HDR
{
    char magic[4];
    int version;
    int dataSize;
};

#pragma pack(pop)

const char EXPECTED_MAGIC[4] = { 'M', 'Y', 'F', 'T' };

//================== GHI FILE ==================
bool WriteBinaryFile(
    const char* path,
    const FILE_HDR& hdr,
    const char* data)
{
    ofstream ofs(path, ios::binary);

    if (!ofs)
        return false;

    ofs.write(reinterpret_cast<const char*>(&hdr), sizeof(FILE_HDR));

    if (!ofs)
        return false;

    if (hdr.dataSize > 0)
    {
        ofs.write(data, hdr.dataSize);

        if (!ofs)
            return false;
    }

    return true;
}

//================== ĐỌC FILE ==================
bool ReadBinaryFile(const char* path,
    FILE_HDR& hdr,
    char*& data)
{
    ifstream ifs(path, ios::binary); // Mở file dưới dạng nhị phân để đọc

    if (!ifs)
        return false;

    ifs.read(reinterpret_cast<char*>(&hdr), sizeof(FILE_HDR));

    if (!ifs)
        return false;

    // Validate Magic Number
    if (memcmp(hdr.magic, EXPECTED_MAGIC, 4) != 0)
    {
        cout << "Magic Number khong hop le!\n";
        return false;
    }

    if (hdr.dataSize < 0)
        return false;

    data = new char[hdr.dataSize + 1];

    ifs.read(data, hdr.dataSize);

    if (!ifs)
    {
        delete[] data;
        data = nullptr;
        return false;
    }

    data[hdr.dataSize] = '\0';

    return true;
}

int main()
{
    const char* fileName = "demo.bin";
    const char* text = "Ohhh right...!!! Come here.";

    FILE_HDR hdr;

    memcpy(hdr.magic, EXPECTED_MAGIC, 4);
    hdr.version = 1;
    hdr.dataSize = strlen(text);

    if (WriteBinaryFile(fileName, hdr, text))
        cout << "Ghi file thanh cong!\n";
    else
        cout << "Ghi file that bai!\n";

    FILE_HDR readHdr;
    char* data = nullptr;

    if (ReadBinaryFile(fileName, readHdr, data))
    {
        cout << "Magic OK\n";
        cout << "Version : " << readHdr.version << '\n';
        cout << "DataSize: " << readHdr.dataSize << '\n';
        cout << "Data    : " << data << '\n';

        delete[] data;
    }
    else
    {
        cout << "Doc file that bai!\n";
    }

    return 0;
}