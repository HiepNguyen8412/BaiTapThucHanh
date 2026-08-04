#ifndef UNICODE
#define UNICODE
#endif
#define NOMINMAX
#include <Windows.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <iomanip>

#include <limits>
#include <fcntl.h>
#include <io.h>
#include <algorithm>
#include <cstddef>


using namespace std;

// Xóa dấu ngoặc kép khi người dùng kéo thả file
// hoặc nhập đường dẫn dạng: "D:\Bài tập\test.exe"
wstring removeQuotes(wstring path)
{
    if (path.size() >= 2 &&
        path.front() == L'"' &&
        path.back() == L'"')
    {
        path.erase(path.begin());
        path.pop_back();
    }

    return path;
}

//Hàm lấy tên các Section
wstring getSectionName(
    const IMAGE_SECTION_HEADER& section)
{
    wstring name;

    for (size_t i = 0;
        i < IMAGE_SIZEOF_SHORT_NAME;
        i++)
    {
        if (section.Name[i] == '\0')
        {
            break;
        }

        name.push_back(
            static_cast<wchar_t>(section.Name[i])
        );
    }

    if (name.empty())
    {
        return L"(Unnamed)";
    }

    return name;
}

//Định dạng Optional Header
enum class PeFormat
{
    Unknown,
    PE32,
    PE32Plus
};

//Tạo kiểu lưu kết quả Optional Header
struct ParsedOptionalHeader
{
    PeFormat format = PeFormat::Unknown;
    size_t fileOffset = 0;

    IMAGE_OPTIONAL_HEADER32 header32{};
    IMAGE_OPTIONAL_HEADER64 header64{};
};

// Đọc toàn bộ file vào vector byte.
bool loadFile(
    const wstring& filePath,
    vector<uint8_t>& fileData)
{
    const filesystem::path path(filePath);

    // Mở file ở chế độ binary và đặt con trỏ tại cuối file.
    ifstream file(
        path,
        ios::binary | ios::ate
    );

    if (!file.is_open())
    {
        wcerr
            << L"Không thể mở file:\n"
            << filePath << L'\n';

        return false;
    }

    // tellg() tại đây trả về kích thước file.
    const streampos endPosition = file.tellg();

    if (endPosition <= 0)
    {
        wcerr
            << L"File rỗng hoặc không thể lấy kích thước.\n";

        return false;
    }

    const uintmax_t rawFileSize =
        static_cast<uintmax_t>(endPosition);

    // Kiểm tra kích thước có thể biểu diễn bởi size_t không.
    if (rawFileSize >
        static_cast<uintmax_t>(
            numeric_limits<size_t>::max()))
    {
        wcerr << L"File quá lớn để xử lý.\n";
        return false;
    }

    const size_t fileSize =
        static_cast<size_t>(rawFileSize);

    fileData.resize(fileSize);

    // Quay lại đầu file.
    file.seekg(0, ios::beg);

    if (!file)
    {
        wcerr << L"Không thể di chuyển về đầu file.\n";
        fileData.clear();
        return false;
    }

    // Đọc toàn bộ dữ liệu file.
    if (!file.read(
        reinterpret_cast<char*>(fileData.data()),
        static_cast<streamsize>(fileData.size())))
    {
        wcerr << L"Không thể đọc toàn bộ file.\n";
        fileData.clear();
        return false;
    }

    return true;
}

//Kiểm tra 1 vùng dữ liệu có nằm hoàn toàn trong file không.
bool isRangeValid(
    size_t offset,
    size_t requiredSize,
    size_t fileSize)
{
    return offset <= fileSize &&
        requiredSize <= fileSize - offset;
}

//Kiểm tra DOS Header và NT Signature
bool parseDosHeader(
    const vector<uint8_t>& fileData,
    IMAGE_DOS_HEADER& dosHeader,
    size_t& ntHeaderOffset)
{
    //Check file có đủ kích thước DOS HEADER khong 
    if (!isRangeValid(
            0,
            sizeof(IMAGE_DOS_HEADER),
            fileData.size()))
    {
        wcerr << L"File quá nhỏ đẻ chứa DOS Header.\n";

        return false;
    }

    //Đọc DOS Header từ đầu file 
    memcpy(&dosHeader,
        fileData.data(),
        sizeof(IMAGE_DOS_HEADER)
    );

    if (dosHeader.e_magic != IMAGE_DOS_SIGNATURE)
    {
        wcerr << L"Không tìm thấy chữ ký MZ.\n"
            << L"Đây không phải là file PE hợp lệ.\n";

        return false;
    }

    // e_lfanew khong dc am.
    if (dosHeader.e_lfanew < 0)
    {
        wcerr << L"Giá trị e_lfanew không hợp lệ.\n";

        return false;
    }

    ntHeaderOffset =
        static_cast<size_t>(dosHeader.e_lfanew);

    if (!isRangeValid(
        ntHeaderOffset,
        sizeof(DWORD),
        fileData.size()))
    {
        wcerr << L"e_lfanew trỏ ra ngoài phạm vi file.\n";

        return false;
    }

    DWORD ntSignature = 0;

    memcpy(
        &ntSignature,
        fileData.data() + ntHeaderOffset,
        sizeof(DWORD)
    );
    
    //Kiểm tra PE\\0\\0
    if (ntSignature != IMAGE_NT_SIGNATURE)
    {
        wcerr << L"Không tìm thấy chữ ký PE\\0\\0.\n";

        return false;
    }

    return true;
}

bool parseFileHeader(
    const vector<uint8_t>& fileData,
    size_t ntHeaderOffset,
    IMAGE_FILE_HEADER& fileHeader,
    size_t& fileHeaderOffset)
{
    //File Header nằm ngay sau NT Signature PE\\0\\0
    fileHeaderOffset = 
        ntHeaderOffset + sizeof(DWORD);

    if(!isRangeValid(
        fileHeaderOffset,
        sizeof(IMAGE_FILE_HEADER),
        fileData.size()))
    {
        wcerr 
            << L"File Header nằm ngoài phạm vi file.\n";

        return false;
    }

    memcpy(
        &fileHeader,
        fileData.data() + fileHeaderOffset,
        sizeof(IMAGE_FILE_HEADER)
    );

    return true;
}

//Hàm xác định kiến trúc CPU
const wchar_t* getMachineName(WORD machine)
{
    switch (machine)
    {
        case IMAGE_FILE_MACHINE_I386:
            return L"x86";
        
        case IMAGE_FILE_MACHINE_AMD64:
            return L"x64";
        
        case IMAGE_FILE_MACHINE_ARM:
            return L"ARM";

        case IMAGE_FILE_MACHINE_ARM64:
            return L"ARM64";

        case IMAGE_FILE_MACHINE_IA64:
            return L"Intel Itanium";

        case IMAGE_FILE_MACHINE_UNKNOWN:
            return L"Unknown";

        default:
            return L"Unsupported";
    }
}

//Optional Header + DATA DIRECTORY
bool parseOptionalHeader(
    const vector<uint8_t>& fileData,
    const IMAGE_FILE_HEADER& fileHeader,
    size_t fileHeaderOffset,
    ParsedOptionalHeader& optionalHeader)
{
    //Tính offset của OptionalHeader
    const size_t optionalHeaderOffset =
        fileHeaderOffset + sizeof(IMAGE_FILE_HEADER);
    
    optionalHeader.fileOffset = optionalHeaderOffset;

    //Optional Header phải chưa tối thiểu trường Magic 
    if (fileHeader.SizeOfOptionalHeader < sizeof(WORD))
    {
        wcerr
            << L"Optional Header quá nhỏ để chưa Magic.\n";

        return false;
    }

    // Kiểm tra toàn bộ Optional Header có nằm trong file không
    if (!isRangeValid(
            optionalHeaderOffset,
            fileHeader.SizeOfOptionalHeader,
            fileData.size()
    ))
    {
        wcerr
            << L"Optional Header nằm ngoài phạm vi file.\n";

        return false;
    }

    WORD magic = 0;

    memcpy(
        &magic,
        fileData.data() + optionalHeaderOffset,
        sizeof(WORD)
    );

    if (magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC)
    {
        if (fileHeader.SizeOfOptionalHeader <
            sizeof(IMAGE_OPTIONAL_HEADER32))
        {
            wcerr
                << L"Optional Header PE32 không đầy đủ.\n";

            return false;
        }

        memcpy(
            &optionalHeader.header32,
            fileData.data() + optionalHeaderOffset,
            sizeof(IMAGE_OPTIONAL_HEADER32)
        );
    
        optionalHeader.format = PeFormat::PE32;
        return true;
    }

    if (magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC)
    {
        if (fileHeader.SizeOfOptionalHeader <
            sizeof(IMAGE_OPTIONAL_HEADER64))
        {
            wcerr
                << L"Optional Header PE32+ không đầy đủ.\n";

            return false;
        }

        memcpy(
            &optionalHeader.header64,
            fileData.data() + optionalHeaderOffset,
            sizeof(IMAGE_OPTIONAL_HEADER64)
        );

        optionalHeader.format = PeFormat::PE32Plus;

        return true;
    }

    wcerr
        << L"Optional Header Magic không hợp lệ: 0x"
        << hex
        << uppercase
        << magic
        << L"\n";

    return false;
}

//Chuyển RVA sang File Offset
//Dùng để parse Export, Import, Resource và Relocation Directory.
DWORD getSizeOfHeaders(
    const ParsedOptionalHeader& optionalHeader)
{
    if (optionalHeader.format == PeFormat::PE32)
    {
        return optionalHeader.header32.SizeOfHeaders;
    }

    if (optionalHeader.format == PeFormat::PE32Plus)
    {
        return optionalHeader.header64.SizeOfHeaders;
    }

    return 0;
}

//Hàm lấy một Data Directory
bool getDataDirectory(
const ParsedOptionalHeader& optionalHeader,
size_t directoryIndex,
IMAGE_DATA_DIRECTORY& directory)
{
    if (directoryIndex >= IMAGE_NUMBEROF_DIRECTORY_ENTRIES)
    {
        return false;
    }

    if (optionalHeader.format == PeFormat::PE32)
    {
        if (directoryIndex >=
            optionalHeader.header32.NumberOfRvaAndSizes)
        {
            return false;
        }

        directory =
            optionalHeader.header32.DataDirectory[
                directoryIndex
            ];

        return true;
    }

    if (optionalHeader.format == PeFormat::PE32Plus)
    {
        if (directoryIndex >=
            optionalHeader.header64.NumberOfRvaAndSizes)
        {
            return false;
        }

        directory =
            optionalHeader.header64.DataDirectory[
                directoryIndex
            ];

        return true;
    }

    return false;
}

//Đọc chuỗi ASCII an toàn từ file
bool readAsciiString(
    const vector<uint8_t>& fileData,
    size_t fileOffset,
    string& result,
    size_t maximumLength = 4096)
{
    result.clear();

    if (fileOffset >= fileData.size())
    {
        return false;
    }

    for (size_t i = 0;
        i < maximumLength &&
        fileOffset + i < fileData.size();
        ++i)
    {
        const char character =
            static_cast<char>(fileData[fileOffset + i]);

        if (character == '\0')
        {
            return true;
        }

        result.push_back(character);
    }

    return false;
}

void printAsciiString(const string& text)
{
    for (unsigned char character : text)
    {
        wcout
            << static_cast<wchar_t>(character);
    }
}


//Tính địa chỉ dự kiến trong RAM 
// VA = ImageBase + RVA
ULONGLONG getImageBase(
    const ParsedOptionalHeader& optionalHeader)
{
    if (optionalHeader.format == PeFormat::PE32)
    {
        return optionalHeader.header32.ImageBase;
    }

    if (optionalHeader.format == PeFormat::PE32Plus)
    {
        return optionalHeader.header64.ImageBase;
    }

    return 0;
}

// Section Headers
bool parseSectionHeaders(
    const vector<uint8_t>& fileData,
    const IMAGE_FILE_HEADER& fileHeader,
    const ParsedOptionalHeader& optionalHeader,
    vector<IMAGE_SECTION_HEADER>& sections,
    size_t& sectionTableOffset)
{
    /*
    * Section Header nằm ngay sau toàn bộ
    * của Optional Header.
    */
    if (optionalHeader.fileOffset > fileData.size() ||
        fileHeader.SizeOfOptionalHeader > 
            fileData.size() - optionalHeader.fileOffset)
    {
        wcerr
            << L"Không thể xác định vị trí Section Headers.\n";
        
        return false;
    }

    sectionTableOffset = 
        optionalHeader.fileOffset +
        fileHeader.SizeOfOptionalHeader;

    const size_t numberOfSections = 
        fileHeader.NumberOfSections;
    
    /*
    Kiểm tra phép nhân gián tiếp:
    Số section không được lớn hơn số cấu trúc
    có thể chứa trong phần file còn lại.
    */

    if (sectionTableOffset > fileData.size())
    {
        wcerr
            << L"Section Headers nằm ngoài file.\n";

        return false;
    }

    const size_t remainingSize = 
        fileData.size() - sectionTableOffset;
      
    const size_t maximumSectionCount = 
        remainingSize / sizeof(IMAGE_SECTION_HEADER);
    
    if (numberOfSections > maximumSectionCount)
    {
        wcerr
            << L"Số lượng Section Headers không hợp lệ.\n";

        return false;
    }

    sections.resize(numberOfSections);

    if (!sections.empty())
    {
        memcpy(
            sections.data(),
            fileData.data() + sectionTableOffset,
            numberOfSections *
                sizeof(IMAGE_SECTION_HEADER)
        );
    }
    
    return true;
    
}


//Hàm chuyển RVA sang File Offset
bool rvaToFileOffset(
DWORD rva,
size_t requiredSize,
const vector<IMAGE_SECTION_HEADER>& sections,
DWORD sizeOfHeaders,
size_t fileSize,
size_t& fileOffset,
size_t* matchedSectionIndex = nullptr)
{
    // RVA bằng 0 thường có nghĩa directory không tồn tại.
    if (rva == 0)
    {
        return false;
    }

    /*
     * Nếu RVA nằm trong vùng PE Headers,
     * RVA cũng chính là File Offset.
     */
    if (rva < sizeOfHeaders)
    {
        const size_t headerOffset =
            static_cast<size_t>(rva);

        if (!isRangeValid(
            headerOffset,
            requiredSize,
            fileSize))
        {
            return false;
        }

        fileOffset = headerOffset;

        if (matchedSectionIndex != nullptr)
        {
            // Không thuộc section nào, mà thuộc vùng Headers.
            *matchedSectionIndex =
                numeric_limits<size_t>::max();
        }

        return true;
    }

    for (size_t i = 0;
        i < sections.size();
        ++i)
    {
        const IMAGE_SECTION_HEADER& section =
            sections[i];

        const DWORD sectionSize = max(
            section.Misc.VirtualSize,
            section.SizeOfRawData
        );

        // Tránh phép trừ bị underflow.
        if (rva < section.VirtualAddress)
        {
            continue;
        }

        const DWORD relativeOffset =
            rva - section.VirtualAddress;

        /*
         * Kiểm tra RVA có thuộc phạm vi section
         * trong bộ nhớ hay không.
         */
        if (relativeOffset >= sectionSize)
        {
            continue;
        }

        /*
         * RVA thuộc section nhưng nằm ngoài dữ liệu raw.
         * Phần này chỉ tồn tại trong RAM và được điền 0.
         */
        if (relativeOffset >= section.SizeOfRawData)
        {
            return false;
        }

        const uint64_t calculatedOffset =
            static_cast<uint64_t>(
                section.PointerToRawData
                ) +
            relativeOffset;

        if (calculatedOffset >
            numeric_limits<size_t>::max())
        {
            return false;
        }

        const size_t resultOffset =
            static_cast<size_t>(
                calculatedOffset
                );

        // Kiểm tra vùng dữ liệu thực sự nằm trong file.
        if (!isRangeValid(
            resultOffset,
            requiredSize,
            fileSize))
        {
            return false;
        }

        fileOffset = resultOffset;

        if (matchedSectionIndex != nullptr)
        {
            *matchedSectionIndex = i;
        }

        return true;
    }

    // Không có section nào chứa RVA này.
    return false;
}

//Hàm kiểm tra Descriptor kết thúc
bool isImportDescriptorEmpty(
    const IMAGE_IMPORT_DESCRIPTOR& descriptor)
{
    return descriptor.OriginalFirstThunk == 0 &&
        descriptor.TimeDateStamp == 0 &&
        descriptor.ForwarderChain == 0 &&
        descriptor.Name == 0 &&
        descriptor.FirstThunk == 0;
}

//Hàm đọc danh sách import PE32
void printImportFunctions32(
const vector<uint8_t>& fileData,
const vector<IMAGE_SECTION_HEADER>& sections,
DWORD sizeOfHeaders,
DWORD lookupTableRva,
DWORD iatRva)
{
    const size_t maximumEntries =
        fileData.size() / sizeof(IMAGE_THUNK_DATA32);

    for (size_t i = 0; i < maximumEntries; ++i)
    {
        const uint64_t thunkRva64 =
            static_cast<uint64_t>(lookupTableRva) +
            i * sizeof(IMAGE_THUNK_DATA32);

        if (thunkRva64 > numeric_limits<DWORD>::max())
        {
            wcout << L"        [!] Thunk RVA bị tràn.\n";
            break;
        }

        const DWORD thunkRva =
            static_cast<DWORD>(thunkRva64);

        size_t thunkFileOffset = 0;

        if (!rvaToFileOffset(
            thunkRva,
            sizeof(IMAGE_THUNK_DATA32),
            sections,
            sizeOfHeaders,
            fileData.size(),
            thunkFileOffset))
        {
            wcout
                << L"        [!] Import Thunk không hợp lệ.\n";
            break;
        }

        IMAGE_THUNK_DATA32 thunk{};

        memcpy(
            &thunk,
            fileData.data() + thunkFileOffset,
            sizeof(IMAGE_THUNK_DATA32)
        );

        const DWORD thunkValue =
            thunk.u1.AddressOfData;

        // Kết thúc bảng thunk.
        if (thunkValue == 0)
        {
            break;
        }

        const uint64_t currentIatRva64 =
            static_cast<uint64_t>(iatRva) +
            i * sizeof(IMAGE_THUNK_DATA32);

        wcout
            << L"        ["
            << dec
            << i
            << L"] ";

        /*
         * Import bằng Ordinal.
         */
        if (IMAGE_SNAP_BY_ORDINAL32(thunkValue))
        {
            const WORD ordinal =
                IMAGE_ORDINAL32(thunkValue);

            wcout
                << L"Ordinal #"
                << dec
                << ordinal
                << L'\n';
        }
        /*
         * Import bằng tên.
         */
        else
        {
            size_t importByNameOffset = 0;

            if (!rvaToFileOffset(
                thunkValue,
                sizeof(WORD) + 1,
                sections,
                sizeOfHeaders,
                fileData.size(),
                importByNameOffset))
            {
                wcout << L"(Invalid function name)\n";
                continue;
            }

            WORD hint = 0;

            memcpy(
                &hint,
                fileData.data() + importByNameOffset,
                sizeof(WORD)
            );

            string functionName;

            if (!readAsciiString(
                fileData,
                importByNameOffset + sizeof(WORD),
                functionName))
            {
                wcout << L"(Invalid function name)\n";
                continue;
            }

            printAsciiString(functionName);

            wcout
                << L"\n"
                << L"            Hint        : "
                << dec
                << hint
                << L'\n';
        }

        wcout
            << hex
            << uppercase
            << setfill(L'0')

            << L"            Lookup RVA  : 0x"
            << setw(8)
            << thunkRva
            << L'\n';

        if (currentIatRva64 <=
            numeric_limits<DWORD>::max())
        {
            wcout
                << L"            IAT RVA     : 0x"
                << setw(8)
                << static_cast<DWORD>(currentIatRva64)
                << L'\n';
        }

        wcout << L'\n';
    }
}

//Hàm đọc danh sách Import PE32+
void printImportFunctions64(
    const vector<uint8_t>& fileData,
    const vector<IMAGE_SECTION_HEADER>& sections,
    DWORD sizeOfHeaders,
    DWORD lookupTableRva,
    DWORD iatRva)
{
    const size_t maximumEntries =
        fileData.size() / sizeof(IMAGE_THUNK_DATA64);

    for (size_t i = 0; i < maximumEntries; ++i)
    {
        const uint64_t thunkRva64 =
            static_cast<uint64_t>(lookupTableRva) +
            i * sizeof(IMAGE_THUNK_DATA64);

        if (thunkRva64 > numeric_limits<DWORD>::max())
        {
            wcout << L"        [!] Thunk RVA bị tràn.\n";
            break;
        }

        const DWORD thunkRva =
            static_cast<DWORD>(thunkRva64);

        size_t thunkFileOffset = 0;

        if (!rvaToFileOffset(
            thunkRva,
            sizeof(IMAGE_THUNK_DATA64),
            sections,
            sizeOfHeaders,
            fileData.size(),
            thunkFileOffset))
        {
            wcout
                << L"        [!] Import Thunk không hợp lệ.\n";
            break;
        }

        IMAGE_THUNK_DATA64 thunk{};

        memcpy(
            &thunk,
            fileData.data() + thunkFileOffset,
            sizeof(IMAGE_THUNK_DATA64)
        );

        const ULONGLONG thunkValue =
            thunk.u1.AddressOfData;

        if (thunkValue == 0)
        {
            break;
        }

        const uint64_t currentIatRva64 =
            static_cast<uint64_t>(iatRva) +
            i * sizeof(IMAGE_THUNK_DATA64);

        wcout
            << L"        ["
            << dec
            << i
            << L"] ";

        /*
         * Import bằng Ordinal.
         */
        if (IMAGE_SNAP_BY_ORDINAL64(thunkValue))
        {
            const WORD ordinal =
                static_cast<WORD>(
                    IMAGE_ORDINAL64(thunkValue)
                    );

            wcout
                << L"Ordinal #"
                << dec
                << ordinal
                << L'\n';
        }
        /*
         * Import bằng tên.
         */
        else
        {
            if (thunkValue >
                numeric_limits<DWORD>::max())
            {
                wcout << L"(Invalid name RVA)\n";
                continue;
            }

            const DWORD importByNameRva =
                static_cast<DWORD>(thunkValue);

            size_t importByNameOffset = 0;

            if (!rvaToFileOffset(
                importByNameRva,
                sizeof(WORD) + 1,
                sections,
                sizeOfHeaders,
                fileData.size(),
                importByNameOffset))
            {
                wcout << L"(Invalid function name)\n";
                continue;
            }

            WORD hint = 0;

            memcpy(
                &hint,
                fileData.data() + importByNameOffset,
                sizeof(WORD)
            );

            string functionName;

            if (!readAsciiString(
                fileData,
                importByNameOffset + sizeof(WORD),
                functionName))
            {
                wcout << L"(Invalid function name)\n";
                continue;
            }

            printAsciiString(functionName);

            wcout
                << L"\n"
                << L"            Hint        : "
                << dec
                << hint
                << L'\n';
        }

        wcout
            << hex
            << uppercase
            << setfill(L'0')

            << L"            Lookup RVA  : 0x"
            << setw(8)
            << thunkRva
            << L'\n';

        if (currentIatRva64 <=
            numeric_limits<DWORD>::max())
        {
            wcout
                << L"            IAT RVA     : 0x"
                << setw(8)
                << static_cast<DWORD>(currentIatRva64)
                << L'\n';
        }

        wcout << L'\n';
    }
}


//Hàm lấy tên Subsystem
const wchar_t* getSubsystemName(WORD subsystem)
{
    switch (subsystem)
    {
    case IMAGE_SUBSYSTEM_NATIVE:
        return L"Native";

    case IMAGE_SUBSYSTEM_WINDOWS_GUI:
        return L"Windows GUI";

    case IMAGE_SUBSYSTEM_WINDOWS_CUI:
        return L"Windows Console";

    case IMAGE_SUBSYSTEM_EFI_APPLICATION:
        return L"EFI Application";

    case IMAGE_SUBSYSTEM_EFI_BOOT_SERVICE_DRIVER:
        return L"EFI Boot Service Driver";

    case IMAGE_SUBSYSTEM_EFI_RUNTIME_DRIVER:
        return L"EFI Runtime Driver";

    case IMAGE_SUBSYSTEM_WINDOWS_BOOT_APPLICATION:
        return L"Windows Boot Application";

    default:
        return L"Unknown"; 
    }
}

const wchar_t* getDataDirectoryName(size_t index)
{
    static const wchar_t* names[
        IMAGE_NUMBEROF_DIRECTORY_ENTRIES] = 
    {
        L"Export Directory",
        L"Import Directory",
        L"Resource Directory",
        L"Exception Directory",
        L"Security Directory",
        L"Base Relocation Directory",
        L"Debug Directory",
        L"Architecture Directory",
        L"Global Pointer Directory",
        L"TLS Directory",
        L"Load Config Directory",
        L"Bound Import Directory",
        L"Import Address Table",
        L"Delay Import Directory",
        L"CLR Runtime Header",
        L"Reserved"
    };

    if (index >= IMAGE_NUMBEROF_DIRECTORY_ENTRIES)
    {
        return L"Unkonown";
    }
    return names[index];
}

wstring getSectionPermissions(
    DWORD characteristics)
{
    wstring permissions;

    permissions +=
        (characteristics & IMAGE_SCN_MEM_READ)
        ? L'R'
        : L'-';

    permissions +=
        (characteristics & IMAGE_SCN_MEM_WRITE)
        ? L'W'
        : L'-';

    permissions +=
        (characteristics & IMAGE_SCN_MEM_EXECUTE)
        ? L'X'
        : L'-';

    return permissions;
}


//Hiển thị DOS HEADER 
void printDosHeader(
    const IMAGE_DOS_HEADER& dosHeader,
    size_t ntHeaderOffset)
{
    wcout
        << L"\n┌────────────────────────────────────────────────────────┐\n"
        << L"│                       DOS HEADER                       │\n"
        << L"└────────────────────────────────────────────────────────┘\n";

    wcout 
        << hex << uppercase << setfill(L'0');

    wcout 
        << L"[+] File Offset: 0x" << setw(8) << 0
        << L"| Size : 0x" << setw(8) << sizeof(IMAGE_DOS_HEADER)
        << L"\n\n";
    //Chi tiết 2 cột
    wcout 
        << L"e_magic :0x" 
        << setw(4) 
        << dosHeader.e_magic << L"(MZ)\n";
    
    wcout   
        << L"e_lfanew : 0x"
        << setw(8)
        << ntHeaderOffset
        << L"  (NT Headers file offset)\n";
    
    wcout
        << L"\n┌──────────────────────────────────────────────────────────┐\n"
        << L"│                       NT SIGNATURE                       │\n"
        << L"└──────────────────────────────────────────────────────────┘\n";

    wcout 
        << L"[+] File Offset : 0x"
        << setw(8)
        << ntHeaderOffset

        << L"    Size : 0x"
        << setw(8)
        << sizeof(DWORD)
        << L"\n\n";

    wcout
        << L"    Signature : 0x"
        << setw(8)
        << IMAGE_NT_SIGNATURE
        << L"  (PE\\0\\0)\n";

    // Khôi phục định dạng mặc định.
    wcout
        << dec
        << setfill(L' ');
}

//Hiển thị FILE HEADER
void printFileHeader(
    const IMAGE_FILE_HEADER& fileHeader,
    size_t fileHeaderOffset)
{
    wcout
        << L"\n┌──────────────────────────────────────────────────────────┐\n"
        << L"│                       FILE HEADER                        │\n"
        << L"└──────────────────────────────────────────────────────────┘\n";

    wcout
        << hex
        << uppercase
        << setfill(L'0');

    wcout   
        << L"[+] File Offset : 0x"
        << setw(8)
        << fileHeaderOffset

        << L"    Size : 0x"
        << setw(8)
        << sizeof(IMAGE_FILE_HEADER)
        << L"\n\n";
    
    wcout 
        << L"    Machine              : 0x"
        << setw(4)
        << static_cast<unsigned int>(fileHeader.Machine)
        << L"   ("
        << getMachineName(fileHeader.Machine)
        <<L"    )\n";

    wcout
        << L"   NumberOfSections       : 0x"
        << setw(4)
        << static_cast<unsigned int>(
            fileHeader.NumberOfSections)
        << L"   ("
        << dec
        << fileHeader.NumberOfSections
        << L" sections)\n";

    wcout 
        << hex
        << uppercase 
        << setfill(L'0');

    wcout  
        << L"   TimeDateStamp          : 0x"
        << setw(8)
        << fileHeader.TimeDateStamp
        <<L'\n';

    wcout
        << L"   SizeOfOptionalHEader   : 0x"
        << setw(4)
        << static_cast<unsigned int>(
            fileHeader.SizeOfOptionalHeader)
        << L'\n';

    wcout 
        << L"   Characteristics        : 0x"
        << setw(4)
        << static_cast<unsigned int>(
            fileHeader.Characteristics
        )
        << L'\n';

    wcout  
        << dec
        << setfill(L' ');
}

void printOptionalHeader(
    const ParsedOptionalHeader& optionalHeader,
    const IMAGE_FILE_HEADER& fileHeader)
{
    wcout
        << L"\n┌──────────────────────────────────────────────────────────┐\n"
        << L"│                     OPTIONAL HEADER                      │\n"
        << L"└──────────────────────────────────────────────────────────┘\n";

    wcout
        << hex
        << uppercase
        << setfill(L'0');

    wcout
        << L"[+] File Offset : 0x"
        << setw(8)
        << optionalHeader.fileOffset

        << L"    Size : 0x"
        << setw(8)
        << static_cast<unsigned int>(
            fileHeader.SizeOfOptionalHeader)
        << L"\n\n";

    if (optionalHeader.format == PeFormat::PE32)
    {
        const IMAGE_OPTIONAL_HEADER32& header =
            optionalHeader.header32;

        wcout
            << L"    Format                : PE32 (32-bit)\n"

            << L"    Magic                 : 0x"
            << setw(4)
            << static_cast<unsigned int>(header.Magic)
            << L"  (PE32)\n"

            << L"    AddressOfEntryPoint   : 0x"
            << setw(8)
            << header.AddressOfEntryPoint
            << L"  (RVA)\n"

            << L"    BaseOfCode            : 0x"
            << setw(8)
            << header.BaseOfCode
            << L"  (RVA)\n"

            << L"    BaseOfData            : 0x"
            << setw(8)
            << header.BaseOfData
            << L"  (RVA)\n"

            << L"    ImageBase             : 0x"
            << setw(8)
            << header.ImageBase
            << L'\n'

            << L"    SectionAlignment      : 0x"
            << setw(8)
            << header.SectionAlignment
            << L'\n'

            << L"    FileAlignment         : 0x"
            << setw(8)
            << header.FileAlignment
            << L'\n'

            << L"    SizeOfImage           : 0x"
            << setw(8)
            << header.SizeOfImage
            << L'\n'

            << L"    SizeOfHeaders         : 0x"
            << setw(8)
            << header.SizeOfHeaders
            << L'\n'

            << L"    Subsystem             : 0x"
            << setw(4)
            << static_cast<unsigned int>(header.Subsystem)
            << L"  ("
            << getSubsystemName(header.Subsystem)
            << L")\n"

            << L"    DllCharacteristics    : 0x"
            << setw(4)
            << static_cast<unsigned int>(
                header.DllCharacteristics)
            << L'\n'

            << L"    NumberOfRvaAndSizes   : 0x"
            << setw(8)
            << header.NumberOfRvaAndSizes
            << L'\n';
    }
    else if (optionalHeader.format == PeFormat::PE32Plus)
    {
        const IMAGE_OPTIONAL_HEADER64& header =
            optionalHeader.header64;

        wcout
            << L"    Format                : PE32+ (64-bit)\n"

            << L"    Magic                 : 0x"
            << setw(4)
            << static_cast<unsigned int>(header.Magic)
            << L"  (PE32+)\n"

            << L"    AddressOfEntryPoint   : 0x"
            << setw(8)
            << header.AddressOfEntryPoint
            << L"  (RVA)\n"

            << L"    BaseOfCode            : 0x"
            << setw(8)
            << header.BaseOfCode
            << L"  (RVA)\n"

            << L"    ImageBase             : 0x"
            << setw(16)
            << static_cast<unsigned long long>(
                header.ImageBase)
            << L'\n'

            << L"    SectionAlignment      : 0x"
            << setw(8)
            << header.SectionAlignment
            << L'\n'

            << L"    FileAlignment         : 0x"
            << setw(8)
            << header.FileAlignment
            << L'\n'

            << L"    SizeOfImage           : 0x"
            << setw(8)
            << header.SizeOfImage
            << L'\n'

            << L"    SizeOfHeaders         : 0x"
            << setw(8)
            << header.SizeOfHeaders
            << L'\n'

            << L"    Subsystem             : 0x"
            << setw(4)
            << static_cast<unsigned int>(header.Subsystem)
            << L"  ("
            << getSubsystemName(header.Subsystem)
            << L")\n"

            << L"    DllCharacteristics    : 0x"
            << setw(4)
            << static_cast<unsigned int>(
                header.DllCharacteristics)
            << L'\n'

            << L"    NumberOfRvaAndSizes   : 0x"
            << setw(8)
            << header.NumberOfRvaAndSizes
            << L'\n';
    }

    wcout
        << dec
        << setfill(L' ');
}

void printDataDirectories(
    const ParsedOptionalHeader& optionalHeader)
{   
    const IMAGE_DATA_DIRECTORY* directories = nullptr;

    DWORD directoryCount = 0;
    size_t dataDirectoryOffset = 0;
    
    if (optionalHeader.format == PeFormat::PE32)
    {
        directories =
            optionalHeader.header32.DataDirectory;

        directoryCount = min<DWORD>(
            optionalHeader.header32.NumberOfRvaAndSizes,
            IMAGE_NUMBEROF_DIRECTORY_ENTRIES
        );

        dataDirectoryOffset =
            optionalHeader.fileOffset +
            offsetof(
                IMAGE_OPTIONAL_HEADER32,
                DataDirectory
            );
    }
    else if (optionalHeader.format == PeFormat::PE32Plus)
    {
        directories =
            optionalHeader.header64.DataDirectory;

        directoryCount = min<DWORD>(
            optionalHeader.header64.NumberOfRvaAndSizes,
            IMAGE_NUMBEROF_DIRECTORY_ENTRIES
        );

        dataDirectoryOffset =
            optionalHeader.fileOffset +
            offsetof(
                IMAGE_OPTIONAL_HEADER64,
                DataDirectory
            );
    }
    else
    {
        wcerr << L"Không thể đọc Data Directories.\n";
        return;
    }

    wcout
        << L"\n┌──────────────────────────────────────────────────────────┐\n"
        << L"│                    DATA DIRECTORIES                      │\n"
        << L"└──────────────────────────────────────────────────────────┘\n";

    wcout
        << hex
        << uppercase
        << setfill(L'0');

    wcout
        << L"[+] File Offset : 0x"
        << setw(8)
        << dataDirectoryOffset

        << L"    Count : 0x"
        << setw(8)
        << directoryCount
        << L"\n\n";

    for (DWORD i = 0; i < directoryCount; ++i)
    {
        const IMAGE_DATA_DIRECTORY& directory =
            directories[i];

        const size_t entryOffset =
            dataDirectoryOffset +
            static_cast<size_t>(i) *
            sizeof(IMAGE_DATA_DIRECTORY);

        wcout
            << L"[" << dec << i << L"] "
            << getDataDirectoryName(i)
            << L'\n';

        wcout
            << hex
            << uppercase
            << setfill(L'0');

        wcout
            << L"    Entry File Offset : 0x"
            << setw(8)
            << entryOffset
            << L'\n';

        /*
         * Security Directory là ngoại lệ:
         * VirtualAddress của nó là File Offset,
         * không phải RVA.
         */
        if (i == IMAGE_DIRECTORY_ENTRY_SECURITY)
        {
            wcout
                << L"    File Offset       : 0x"
                << setw(8)
                << directory.VirtualAddress
                << L'\n';
        }
        else
        {
            wcout
                << L"    RVA               : 0x"
                << setw(8)
                << directory.VirtualAddress
                << L'\n';
        }

        wcout
            << L"    Size              : 0x"
            << setw(8)
            << directory.Size;

        if (directory.VirtualAddress == 0 &&
            directory.Size == 0)
        {
            wcout << L"  (Not present)";
        }

        wcout << L"\n\n";
    }

    wcout
        << dec
        << setfill(L' ');
}

//Hiển thị Section Headers
void printSectionHeaders(
    const vector<IMAGE_SECTION_HEADER>& sections,
    size_t sectionTableOffset)
{
    wcout
        << L"\n┌──────────────────────────────────────────────────────────┐\n"
        << L"│                    SECTION HEADERS                       │\n"
        << L"└──────────────────────────────────────────────────────────┘\n";

    wcout
        << hex
        << uppercase
        << setfill(L'0');

    wcout
        << L"[+] File Offset : 0x"
        << setw(8)
        << sectionTableOffset

        << L"    Count : 0x"
        << setw(8)
        << sections.size()

        << L"    Size : 0x"
        << setw(8)
        << sections.size() *
            sizeof(IMAGE_SECTION_HEADER)
        << L"\n\n";

    for (size_t i = 0;
         i < sections.size();
         ++i)
    {
        const IMAGE_SECTION_HEADER& section =
            sections[i];

        const size_t currentHeaderOffset =
            sectionTableOffset +
            i * sizeof(IMAGE_SECTION_HEADER);

        wcout
            << L"┌─ Section ["
            << dec
            << i
            << L"] "
            << getSectionName(section)
            << L"\n";

        wcout
            << hex
            << uppercase
            << setfill(L'0');

        wcout
            << L"│  Header File Offset : 0x"
            << setw(8)
            << currentHeaderOffset
            << L'\n';

        wcout
            << L"│  VirtualAddress     : 0x"
            << setw(8)
            << section.VirtualAddress
            << L"  (RVA)\n";

        wcout
            << L"│  VirtualSize        : 0x"
            << setw(8)
            << section.Misc.VirtualSize
            << L'\n';

        wcout
            << L"│  PointerToRawData   : 0x"
            << setw(8)
            << section.PointerToRawData
            << L"  (File Offset)\n";

        wcout
            << L"│  SizeOfRawData      : 0x"
            << setw(8)
            << section.SizeOfRawData
            << L'\n';

        wcout
            << L"│  Characteristics    : 0x"
            << setw(8)
            << section.Characteristics
            << L"  ("
            << getSectionPermissions(
                section.Characteristics)
            << L")\n";

        wcout
            << L"└──────────────────────────────────────────────────────────\n\n";
    }

    wcout
        << dec
        << setfill(L' ');
}

//Kết quả chuyển đổi RVA sang fileOffset
void printRvaMapping(
    const wchar_t* title,
    DWORD rva,
    const ParsedOptionalHeader& optionalHeader,
    const vector<IMAGE_SECTION_HEADER>& sections,
    size_t fileSize)
{
    size_t fileOffset = 0;
    size_t sectionIndex = 0;

    const DWORD sizeOfHeaders =
        getSizeOfHeaders(optionalHeader);

    const ULONGLONG imageBase =
        getImageBase(optionalHeader);

    wcout
        << L"\n[+] "
        << title
        << L'\n';

    wcout
        << hex
        << uppercase
        << setfill(L'0');

    wcout
        << L"    RVA         : 0x"
        << setw(8)
        << rva
        << L'\n';

    wcout
        << L"    VA          : 0x"
        << setw(16)
        << static_cast<unsigned long long>(
            imageBase + rva)
        << L'\n';

    if (!rvaToFileOffset(
        rva,
        1,
        sections,
        sizeOfHeaders,
        fileSize,
        fileOffset,
        &sectionIndex))
    {
        wcout
            << L"    File Offset : Không có dữ liệu raw "
            << L"tương ứng trong file\n";

        wcout
            << dec
            << setfill(L' ');

        return;
    }

    wcout
        << L"    File Offset : 0x"
        << setw(8)
        << fileOffset
        << L'\n';

    if (sectionIndex ==
        numeric_limits<size_t>::max())
    {
        wcout
            << L"    Location    : PE Headers\n";
    }
    else
    {
        wcout
            << L"    Section     : "
            << getSectionName(
                sections[sectionIndex])
            << L'\n';
    }

    wcout
        << dec
        << setfill(L' ');
}

//Kiểm tra bằng Entry Point
DWORD getEntryPointRva(
    const ParsedOptionalHeader& optionalHeader)
{
    if (optionalHeader.format == PeFormat::PE32)
    {
        return optionalHeader
            .header32
            .AddressOfEntryPoint;
    }

    if (optionalHeader.format == PeFormat::PE32Plus)
    {
        return optionalHeader
            .header64
            .AddressOfEntryPoint;
    }

    return 0;
}

//Bảng hiển thị Export Directory
void printExportDirectory(
    const vector<uint8_t>& fileData,
    const ParsedOptionalHeader& optionalHeader,
    const vector<IMAGE_SECTION_HEADER>& sections)
{
    IMAGE_DATA_DIRECTORY exportData{};

    if (!getDataDirectory(
        optionalHeader,
        IMAGE_DIRECTORY_ENTRY_EXPORT,
        exportData))
    {
        wcout
            << L"\n[!] Không thể lấy Export Directory.\n";
        return;
    }

    wcout
        << L"\n┌──────────────────────────────────────────────────────────┐\n"
        << L"│                    EXPORT DIRECTORY                      │\n"
        << L"└──────────────────────────────────────────────────────────┘\n";

    wcout
        << hex
        << uppercase
        << setfill(L'0');

    if (exportData.VirtualAddress == 0 ||
        exportData.Size == 0)
    {
        wcout
            << L"[!] File không có Export Directory.\n";

        wcout
            << dec
            << setfill(L' ');

        return;
    }

    const DWORD sizeOfHeaders =
        getSizeOfHeaders(optionalHeader);

    size_t exportDirectoryOffset = 0;

    if (!rvaToFileOffset(
        exportData.VirtualAddress,
        sizeof(IMAGE_EXPORT_DIRECTORY),
        sections,
        sizeOfHeaders,
        fileData.size(),
        exportDirectoryOffset))
    {
        wcout
            << L"[!] Export Directory RVA không hợp lệ.\n";

        wcout
            << dec
            << setfill(L' ');

        return;
    }

    IMAGE_EXPORT_DIRECTORY exportDirectory{};

    memcpy(
        &exportDirectory,
        fileData.data() + exportDirectoryOffset,
        sizeof(IMAGE_EXPORT_DIRECTORY)
    );

    wcout
        << L"[+] RVA         : 0x"
        << setw(8)
        << exportData.VirtualAddress

        << L"    File Offset : 0x"
        << setw(8)
        << exportDirectoryOffset

        << L"    Size : 0x"
        << setw(8)
        << exportData.Size
        << L"\n\n";

    /*
     * Đọc tên module/DLL.
     */
    string moduleName;
    size_t moduleNameOffset = 0;

    if (exportDirectory.Name != 0 &&
        rvaToFileOffset(
            exportDirectory.Name,
            1,
            sections,
            sizeOfHeaders,
            fileData.size(),
            moduleNameOffset) &&
        readAsciiString(
            fileData,
            moduleNameOffset,
            moduleName))
    {
        wcout << L"    Module Name       : ";
        printAsciiString(moduleName);
        wcout << L'\n';
    }
    else
    {
        wcout
            << L"    Module Name       : (Invalid)\n";
    }

    wcout
        << L"    Ordinal Base      : 0x"
        << setw(8)
        << exportDirectory.Base
        << L'\n'

        << L"    NumberOfFunctions : 0x"
        << setw(8)
        << exportDirectory.NumberOfFunctions
        << L'\n'

        << L"    NumberOfNames     : 0x"
        << setw(8)
        << exportDirectory.NumberOfNames
        << L"\n\n";

    if (exportDirectory.NumberOfFunctions == 0)
    {
        wcout
            << L"[!] Export Directory không có hàm.\n";

        wcout
            << dec
            << setfill(L' ');

        return;
    }

    /*
     * Kiểm tra kích thước ba bảng:
     *
     * AddressOfFunctions     -> DWORD[]
     * AddressOfNames         -> DWORD[]
     * AddressOfNameOrdinals  -> WORD[]
     */
    const uint64_t functionsTableSize64 =
        static_cast<uint64_t>(
            exportDirectory.NumberOfFunctions
            ) * sizeof(DWORD);

    const uint64_t namesTableSize64 =
        static_cast<uint64_t>(
            exportDirectory.NumberOfNames
            ) * sizeof(DWORD);

    const uint64_t ordinalsTableSize64 =
        static_cast<uint64_t>(
            exportDirectory.NumberOfNames
            ) * sizeof(WORD);

    if (functionsTableSize64 >
        numeric_limits<size_t>::max() ||
        namesTableSize64 >
        numeric_limits<size_t>::max() ||
        ordinalsTableSize64 >
        numeric_limits<size_t>::max())
    {
        wcout
            << L"[!] Kích thước bảng Export không hợp lệ.\n";

        return;
    }

    size_t functionsOffset = 0;
    size_t namesOffset = 0;
    size_t ordinalsOffset = 0;

    if (!rvaToFileOffset(
        exportDirectory.AddressOfFunctions,
        static_cast<size_t>(
            functionsTableSize64),
        sections,
        sizeOfHeaders,
        fileData.size(),
        functionsOffset))
    {
        wcout
            << L"[!] AddressOfFunctions không hợp lệ.\n";
        return;
    }

    /*
     * NumberOfNames có thể bằng 0 nếu các hàm chỉ
     * được Export bằng ordinal.
     */
    if (exportDirectory.NumberOfNames > 0)
    {
        if (!rvaToFileOffset(
            exportDirectory.AddressOfNames,
            static_cast<size_t>(
                namesTableSize64),
            sections,
            sizeOfHeaders,
            fileData.size(),
            namesOffset))
        {
            wcout
                << L"[!] AddressOfNames không hợp lệ.\n";
            return;
        }

        if (!rvaToFileOffset(
            exportDirectory.AddressOfNameOrdinals,
            static_cast<size_t>(
                ordinalsTableSize64),
            sections,
            sizeOfHeaders,
            fileData.size(),
            ordinalsOffset))
        {
            wcout
                << L"[!] AddressOfNameOrdinals "
                << L"không hợp lệ.\n";
            return;
        }
    }

    wcout
        << L"    EXPORTED FUNCTIONS\n"
        << L"    ------------------------------------------------------\n";

    for (DWORD i = 0;
        i < exportDirectory.NumberOfNames;
        ++i)
    {
        DWORD nameRva = 0;
        WORD ordinalIndex = 0;

        memcpy(
            &nameRva,
            fileData.data() +
            namesOffset +
            static_cast<size_t>(i) *
            sizeof(DWORD),
            sizeof(DWORD)
        );

        memcpy(
            &ordinalIndex,
            fileData.data() +
            ordinalsOffset +
            static_cast<size_t>(i) *
            sizeof(WORD),
            sizeof(WORD)
        );

        /*
         * Ordinal index dùng để truy cập
         * bảng AddressOfFunctions.
         */
        if (ordinalIndex >=
            exportDirectory.NumberOfFunctions)
        {
            wcout
                << L"    [!] Ordinal index không hợp lệ.\n";
            continue;
        }

        DWORD functionRva = 0;

        memcpy(
            &functionRva,
            fileData.data() +
            functionsOffset +
            static_cast<size_t>(
                ordinalIndex
                ) * sizeof(DWORD),
            sizeof(DWORD)
        );

        size_t functionNameOffset = 0;
        string functionName;

        if (!rvaToFileOffset(
            nameRva,
            1,
            sections,
            sizeOfHeaders,
            fileData.size(),
            functionNameOffset) ||
            !readAsciiString(
                fileData,
                functionNameOffset,
                functionName))
        {
            functionName = "(Invalid name)";
        }

        const DWORD publicOrdinal =
            exportDirectory.Base + ordinalIndex;

        wcout
            << L"    ["
            << dec
            << i
            << L"] ";

        printAsciiString(functionName);

        wcout
            << hex
            << uppercase
            << setfill(L'0')

            << L"\n        Ordinal      : "
            << dec
            << publicOrdinal

            << hex
            << L"\n        Function RVA : 0x"
            << setw(8)
            << functionRva;

        /*
         * Nếu RVA hàm nằm trong vùng Export Directory,
         * đây thường là Forwarded Export.
         */
        const uint64_t exportStart =
            exportData.VirtualAddress;

        const uint64_t exportEnd =
            exportStart + exportData.Size;

        if (functionRva >= exportStart &&
            functionRva < exportEnd)
        {
            size_t forwarderOffset = 0;
            string forwarderName;

            if (rvaToFileOffset(
                functionRva,
                1,
                sections,
                sizeOfHeaders,
                fileData.size(),
                forwarderOffset) &&
                readAsciiString(
                    fileData,
                    forwarderOffset,
                    forwarderName))
            {
                wcout
                    << L"\n        Forwarded To : ";

                printAsciiString(forwarderName);
            }
        }

        wcout << L"\n\n";
    }

    /*
     * Thông báo nếu file chỉ Export bằng ordinal.
     */
    if (exportDirectory.NumberOfNames == 0)
    {
        wcout
            << L"    File chỉ Export bằng ordinal, "
            << L"không có tên hàm.\n";
    }

    wcout
        << dec
        << setfill(L' ');
}

void printImportDirectory(
    const vector<uint8_t>& fileData,
    const ParsedOptionalHeader& optionalHeader,
    const vector<IMAGE_SECTION_HEADER>& sections)
{

    IMAGE_DATA_DIRECTORY importData{};

    wcout
        << L"\n┌──────────────────────────────────────────────────────────┐\n"
        << L"│                    IMPORT DIRECTORY                      │\n"
        << L"└──────────────────────────────────────────────────────────┘\n";

    if (!getDataDirectory(
        optionalHeader,
        IMAGE_DIRECTORY_ENTRY_IMPORT,
        importData))
    {
        wcout
            << L"[!] Không thể lấy Import Directory.\n";
        return;
    }

    if (importData.VirtualAddress == 0 ||
        importData.Size == 0)
    {
        wcout
            << L"[!] File không có Import Directory.\n";
        return;
    }

    const DWORD sizeOfHeaders =
        getSizeOfHeaders(optionalHeader);

    size_t importDirectoryOffset = 0;

    if (!rvaToFileOffset(
        importData.VirtualAddress,
        sizeof(IMAGE_IMPORT_DESCRIPTOR),
        sections,
        sizeOfHeaders,
        fileData.size(),
        importDirectoryOffset))
    {
        wcout
            << L"[!] Import Directory RVA không hợp lệ.\n";
        return;
    }

    wcout
        << hex
        << uppercase
        << setfill(L'0')

        << L"[+] RVA         : 0x"
        << setw(8)
        << importData.VirtualAddress

        << L"    File Offset : 0x"
        << setw(8)
        << importDirectoryOffset

        << L"    Size : 0x"
        << setw(8)
        << importData.Size
        << L"\n\n";

    const size_t maximumDescriptors =
        importData.Size /
        sizeof(IMAGE_IMPORT_DESCRIPTOR);

    bool foundDescriptor = false;

    for (size_t i = 0;
        i < maximumDescriptors;
        ++i)
    {
        const uint64_t descriptorRva64 =
            static_cast<uint64_t>(
                importData.VirtualAddress
                ) +
            i * sizeof(IMAGE_IMPORT_DESCRIPTOR);

        if (descriptorRva64 >
            numeric_limits<DWORD>::max())
        {
            wcout
                << L"[!] Import Descriptor RVA bị tràn.\n";
            break;
        }

        const DWORD descriptorRva =
            static_cast<DWORD>(descriptorRva64);

        size_t descriptorOffset = 0;

        if (!rvaToFileOffset(
            descriptorRva,
            sizeof(IMAGE_IMPORT_DESCRIPTOR),
            sections,
            sizeOfHeaders,
            fileData.size(),
            descriptorOffset))
        {
            wcout
                << L"[!] Import Descriptor không hợp lệ.\n";
            break;
        }

        IMAGE_IMPORT_DESCRIPTOR descriptor{};

        memcpy(
            &descriptor,
            fileData.data() + descriptorOffset,
            sizeof(IMAGE_IMPORT_DESCRIPTOR)
        );

        // Descriptor toàn 0 đánh dấu kết thúc.
        if (isImportDescriptorEmpty(descriptor))
        {
            break;
        }

        foundDescriptor = true;

        string dllName;
        size_t dllNameOffset = 0;

        const bool validDllName =
            descriptor.Name != 0 &&
            rvaToFileOffset(
                descriptor.Name,
                1,
                sections,
                sizeOfHeaders,
                fileData.size(),
                dllNameOffset) &&
            readAsciiString(
                fileData,
                dllNameOffset,
                dllName);

        wcout
            << L"┌─ DLL ["
            << dec
            << i
            << L"] ";

        if (validDllName)
        {
            printAsciiString(dllName);
        }
        else
        {
            wcout << L"(Invalid DLL name)";
        }

        wcout
            << L'\n'
            << hex
            << uppercase
            << setfill(L'0')

            << L"│  Descriptor RVA       : 0x"
            << setw(8)
            << descriptorRva
            << L'\n'

            << L"│  Descriptor Offset    : 0x"
            << setw(8)
            << descriptorOffset
            << L'\n'

            << L"│  OriginalFirstThunk   : 0x"
            << setw(8)
            << descriptor.OriginalFirstThunk
            << L'\n'

            << L"│  FirstThunk           : 0x"
            << setw(8)
            << descriptor.FirstThunk
            << L'\n'

            << L"│  Name RVA             : 0x"
            << setw(8)
            << descriptor.Name
            << L'\n'

            << L"│\n"
            << L"│  Imported Functions:\n";

        /*
         * Ưu tiên Import Lookup Table.
         * Nếu OriginalFirstThunk bằng 0 thì dùng FirstThunk.
         */
        const DWORD lookupTableRva =
            descriptor.OriginalFirstThunk != 0
            ? descriptor.OriginalFirstThunk
            : descriptor.FirstThunk;

        if (lookupTableRva == 0)
        {
            wcout
                << L"│      [!] Không có bảng Import Thunk.\n";
        }
        else if (optionalHeader.format == PeFormat::PE32)
        {
            printImportFunctions32(
                fileData,
                sections,
                sizeOfHeaders,
                lookupTableRva,
                descriptor.FirstThunk
            );
        }
        else if (
            optionalHeader.format == PeFormat::PE32Plus)
        {
            printImportFunctions64(
                fileData,
                sections,
                sizeOfHeaders,
                lookupTableRva,
                descriptor.FirstThunk
            );
        }

        wcout
            << L"└──────────────────────────────────────────────────────────\n\n";
    }

    if (!foundDescriptor)
    {
        wcout
            << L"[!] Không tìm thấy Import Descriptor.\n";
    }

    wcout
        << dec
        << setfill(L' ');
}

// Đổi Resource ID thành tên dễ đọc.
// Khai báo trước các hàm hỗ trợ Resource.
// Đây chỉ là khai báo, không có thân hàm.

bool resourceRelativeToFileOffset(
    size_t resourceRootOffset,
    size_t resourceSize,
    size_t relativeOffset,
    size_t requiredSize,
    size_t fileSize,
    size_t& fileOffset);

bool readResourceUnicodeName(
    const vector<uint8_t>& fileData,
    size_t resourceRootOffset,
    size_t resourceSize,
    DWORD nameField,
    wstring& result);

const wchar_t* getResourceTypeName(
    WORD resourceType)
{
    switch (resourceType)
    {
    case 1:
        return L"Cursor";

    case 2:
        return L"Bitmap";

    case 3:
        return L"Icon";

    case 4:
        return L"Menu";

    case 5:
        return L"Dialog";

    case 6:
        return L"String Table";

    case 7:
        return L"Font Directory";

    case 8:
        return L"Font";

    case 9:
        return L"Accelerator";

    case 10:
        return L"Raw Data";

    case 11:
        return L"Message Table";

    case 12:
        return L"Group Cursor";

    case 14:
        return L"Group Icon";

    case 16:
        return L"Version Information";

    case 17:
        return L"Dialog Include";

    case 19:
        return L"Plug and Play";

    case 20:
        return L"VXD";

    case 21:
        return L"Animated Cursor";

    case 22:
        return L"Animated Icon";

    case 23:
        return L"HTML";

    case 24:
        return L"Manifest";

    default:
        return L"Unknown";
    }
}

//Hướng đổi offset tương đối của resource thành File Offset
bool resourceRelativeToFileOffset(
    size_t resourceRootOffset,
    size_t resourceSize,
    size_t relativeOffset,
    size_t requiredSize,
    size_t fileSize,
    size_t& fileOffset)
{
    // relativeOffset phải nằm trong Resource Directory.
    if (relativeOffset > resourceSize)
    {
        return false;
    }

    // Vùng cần đọc không được vượt ra ngoài Resource Directory.
    if (requiredSize >
        resourceSize - relativeOffset)
    {
        return false;
    }

    // Kiểm tra phép cộng có bị tràn hay không.
    if (resourceRootOffset >
        numeric_limits<size_t>::max() -
        relativeOffset)
    {
        return false;
    }

    const size_t calculatedOffset =
        resourceRootOffset + relativeOffset;

    // Kiểm tra vùng cuối cùng có nằm trong file hay không.
    if (!isRangeValid(
        calculatedOffset,
        requiredSize,
        fileSize))
    {
        return false;
    }

    fileOffset = calculatedOffset;

    return true;
}



void printResourceDirectoryLevel(
    const vector<uint8_t>& fileData,
    const ParsedOptionalHeader& optionalHeader,
    const vector<IMAGE_SECTION_HEADER>& sections,
    size_t resourceRootOffset,
    size_t resourceSize,
    size_t directoryRelativeOffset,
    size_t depth,
    vector<size_t>& activeDirectories,
    size_t& totalEntries,
    size_t& totalDataEntries)
{
    constexpr size_t maximumDepth = 8;
    constexpr size_t maximumEntries = 10000;

    const wstring indentation(
        depth * 4,
        L' '
    );

    if (depth > maximumDepth)
    {
        wcout
            << indentation
            << L"[!] Resource tree quá sâu.\n";

        return;
    }

    /*
     * Ngăn file PE lỗi tạo vòng lặp directory.
     */
    if (find(
        activeDirectories.begin(),
        activeDirectories.end(),
        directoryRelativeOffset) !=
        activeDirectories.end())
    {
        wcout
            << indentation
            << L"[!] Phát hiện vòng lặp Resource Directory.\n";

        return;
    }

    size_t directoryFileOffset = 0;

    if (!resourceRelativeToFileOffset(
        resourceRootOffset,
        resourceSize,
        directoryRelativeOffset,
        sizeof(IMAGE_RESOURCE_DIRECTORY),
        fileData.size(),
        directoryFileOffset))
    {
        wcout
            << indentation
            << L"[!] Resource Directory không hợp lệ.\n";

        return;
    }

    IMAGE_RESOURCE_DIRECTORY directory{};

    memcpy(
        &directory,
        fileData.data() + directoryFileOffset,
        sizeof(IMAGE_RESOURCE_DIRECTORY)
    );

    const size_t entryCount =
        static_cast<size_t>(
            directory.NumberOfNamedEntries
            ) +
        static_cast<size_t>(
            directory.NumberOfIdEntries
            );

    const uint64_t entriesSize64 =
        static_cast<uint64_t>(entryCount) *
        sizeof(IMAGE_RESOURCE_DIRECTORY_ENTRY);

    if (entriesSize64 >
        numeric_limits<size_t>::max())
    {
        wcout
            << indentation
            << L"[!] Số Resource Entry không hợp lệ.\n";

        return;
    }

    if (directoryRelativeOffset >
        numeric_limits<size_t>::max() -
        sizeof(IMAGE_RESOURCE_DIRECTORY))
    {
        return;
    }

    const size_t entryTableRelativeOffset =
        directoryRelativeOffset +
        sizeof(IMAGE_RESOURCE_DIRECTORY);

    const size_t entriesSize =
        static_cast<size_t>(entriesSize64);

    size_t entryTableFileOffset = 0;

    if (!resourceRelativeToFileOffset(
        resourceRootOffset,
        resourceSize,
        entryTableRelativeOffset,
        entriesSize,
        fileData.size(),
        entryTableFileOffset))
    {
        wcout
            << indentation
            << L"[!] Bảng Resource Entry không hợp lệ.\n";

        return;
    }

    activeDirectories.push_back(
        directoryRelativeOffset
    );

    for (size_t i = 0; i < entryCount; ++i)
    {
        if (totalEntries >= maximumEntries)
        {
            wcout
                << indentation
                << L"[!] Đã đạt giới hạn Resource Entry.\n";

            break;
        }

        ++totalEntries;

        IMAGE_RESOURCE_DIRECTORY_ENTRY entry{};

        memcpy(
            &entry,
            fileData.data() +
            entryTableFileOffset +
            i * sizeof(
                IMAGE_RESOURCE_DIRECTORY_ENTRY),
            sizeof(IMAGE_RESOURCE_DIRECTORY_ENTRY)
        );

        const DWORD nameField = entry.Name;

        const bool nameIsString =
            (nameField & 0x80000000UL) != 0;

        wcout
            << indentation
            << L"├─ ";

        if (nameIsString)
        {
            wstring resourceName;

            if (readResourceUnicodeName(
                fileData,
                resourceRootOffset,
                resourceSize,
                nameField,
                resourceName))
            {
                wcout
                    << L"Name \""
                    << resourceName
                    << L"\"";
            }
            else
            {
                wcout << L"(Invalid Unicode name)";
            }
        }
        else
        {
            const WORD resourceId =
                static_cast<WORD>(
                    nameField & 0xFFFF
                    );

            if (depth == 0)
            {
                wcout
                    << L"Type "
                    << dec
                    << resourceId
                    << L" ("
                    << getResourceTypeName(resourceId)
                    << L")";
            }
            else if (depth == 1)
            {
                wcout
                    << L"Name/ID "
                    << dec
                    << resourceId;
            }
            else if (depth == 2)
            {
                wcout
                    << L"Language ID 0x"
                    << hex
                    << uppercase
                    << setfill(L'0')
                    << setw(4)
                    << resourceId;
            }
            else
            {
                wcout
                    << L"ID "
                    << dec
                    << resourceId;
            }
        }

        const DWORD dataField =
            entry.OffsetToData;

        const bool isDirectory =
            (dataField & 0x80000000UL) != 0;

        const size_t childRelativeOffset =
            static_cast<size_t>(
                dataField & 0x7FFFFFFFUL
                );

        if (isDirectory)
        {
            wcout << L"  [Directory]\n";

            printResourceDirectoryLevel(
                fileData,
                optionalHeader,
                sections,
                resourceRootOffset,
                resourceSize,
                childRelativeOffset,
                depth + 1,
                activeDirectories,
                totalEntries,
                totalDataEntries
            );

            continue;
        }

        wcout << L"  [Data]\n";

        size_t dataEntryFileOffset = 0;

        if (!resourceRelativeToFileOffset(
            resourceRootOffset,
            resourceSize,
            childRelativeOffset,
            sizeof(IMAGE_RESOURCE_DATA_ENTRY),
            fileData.size(),
            dataEntryFileOffset))
        {
            wcout
                << indentation
                << L"    [!] IMAGE_RESOURCE_DATA_ENTRY "
                << L"không hợp lệ.\n";

            continue;
        }

        IMAGE_RESOURCE_DATA_ENTRY dataEntry{};

        memcpy(
            &dataEntry,
            fileData.data() + dataEntryFileOffset,
            sizeof(IMAGE_RESOURCE_DATA_ENTRY)
        );

        ++totalDataEntries;

        const DWORD sizeOfHeaders =
            getSizeOfHeaders(optionalHeader);

        size_t resourceDataFileOffset = 0;

        const bool validData =
            dataEntry.OffsetToData != 0 &&
            rvaToFileOffset(
                dataEntry.OffsetToData,
                dataEntry.Size,
                sections,
                sizeOfHeaders,
                fileData.size(),
                resourceDataFileOffset
            );

        wcout
            << hex
            << uppercase
            << setfill(L'0')

            << indentation
            << L"    Data Entry Offset : 0x"
            << setw(8)
            << dataEntryFileOffset
            << L'\n'

            << indentation
            << L"    Data RVA          : 0x"
            << setw(8)
            << dataEntry.OffsetToData
            << L'\n';

        if (validData)
        {
            wcout
                << indentation
                << L"    Data File Offset  : 0x"
                << setw(8)
                << resourceDataFileOffset
                << L'\n';
        }
        else
        {
            wcout
                << indentation
                << L"    Data File Offset  : (Invalid)\n";
        }

        wcout
            << indentation
            << L"    Data Size         : 0x"
            << setw(8)
            << dataEntry.Size
            << L'\n'

            << indentation
            << L"    Code Page         : 0x"
            << setw(8)
            << dataEntry.CodePage
            << L"\n\n";
    }

    activeDirectories.pop_back();

    wcout
        << dec
        << setfill(L' ');
}

bool readResourceUnicodeName(
    const vector<uint8_t>& fileData,
    size_t resourceRootOffset,
    size_t resourceSize,
    DWORD nameField,
    wstring& result)
{
    result.clear();

    /*
     * Bit cao nhất bằng 1 thì Name chứa offset
     * đến IMAGE_RESOURCE_DIR_STRING_U.
     */
    if ((nameField & 0x80000000UL) == 0)
    {
        return false;
    }

    const size_t nameRelativeOffset =
        static_cast<size_t>(
            nameField & 0x7FFFFFFFUL
            );

    size_t lengthFileOffset = 0;

    if (!resourceRelativeToFileOffset(
        resourceRootOffset,
        resourceSize,
        nameRelativeOffset,
        sizeof(WORD),
        fileData.size(),
        lengthFileOffset))
    {
        return false;
    }

    WORD characterCount = 0;

    memcpy(
        &characterCount,
        fileData.data() + lengthFileOffset,
        sizeof(WORD)
    );

    const uint64_t stringSize64 =
        static_cast<uint64_t>(characterCount) *
        sizeof(WCHAR);

    if (stringSize64 >
        numeric_limits<size_t>::max())
    {
        return false;
    }

    const size_t stringSize =
        static_cast<size_t>(stringSize64);

    const size_t stringRelativeOffset =
        nameRelativeOffset + sizeof(WORD);

    size_t stringFileOffset = 0;

    if (!resourceRelativeToFileOffset(
        resourceRootOffset,
        resourceSize,
        stringRelativeOffset,
        stringSize,
        fileData.size(),
        stringFileOffset))
    {
        return false;
    }

    result.reserve(characterCount);

    for (WORD i = 0; i < characterCount; ++i)
    {
        WCHAR character = 0;

        memcpy(
            &character,
            fileData.data() +
            stringFileOffset +
            static_cast<size_t>(i) *
            sizeof(WCHAR),
            sizeof(WCHAR)
        );

        result.push_back(
            static_cast<wchar_t>(character)
        );
    }

    return true;
}

void printResourceDirectory(
    const vector<uint8_t>& fileData,
    const ParsedOptionalHeader& optionalHeader,
    const vector<IMAGE_SECTION_HEADER>& sections)
{
    wcout
        << L"\n┌──────────────────────────────────────────────────────────┐\n"
        << L"│                   RESOURCE DIRECTORY                     │\n"
        << L"└──────────────────────────────────────────────────────────┘\n";

    IMAGE_DATA_DIRECTORY resourceData{};

    if (!getDataDirectory(
        optionalHeader,
        IMAGE_DIRECTORY_ENTRY_RESOURCE,
        resourceData))
    {
        wcout
            << L"[!] Không thể lấy Resource Directory.\n";

        return;
    }

    if (resourceData.VirtualAddress == 0 ||
        resourceData.Size == 0)
    {
        wcout
            << L"[!] File không có Resource Directory.\n";

        return;
    }

    const DWORD sizeOfHeaders =
        getSizeOfHeaders(optionalHeader);

    size_t resourceRootOffset = 0;

    if (!rvaToFileOffset(
        resourceData.VirtualAddress,
        sizeof(IMAGE_RESOURCE_DIRECTORY),
        sections,
        sizeOfHeaders,
        fileData.size(),
        resourceRootOffset))
    {
        wcout
            << L"[!] Resource Directory RVA không hợp lệ.\n";

        return;
    }

    const size_t resourceSize =
        static_cast<size_t>(resourceData.Size);

    if (!isRangeValid(
        resourceRootOffset,
        resourceSize,
        fileData.size()))
    {
        wcout
            << L"[!] Vùng Resource nằm ngoài phạm vi file.\n";

        return;
    }

    wcout
        << hex
        << uppercase
        << setfill(L'0')

        << L"[+] RVA         : 0x"
        << setw(8)
        << resourceData.VirtualAddress

        << L"    File Offset : 0x"
        << setw(8)
        << resourceRootOffset

        << L"    Size : 0x"
        << setw(8)
        << resourceData.Size
        << L"\n\n";

    vector<size_t> activeDirectories;

    size_t totalEntries = 0;
    size_t totalDataEntries = 0;

    printResourceDirectoryLevel(
        fileData,
        optionalHeader,
        sections,
        resourceRootOffset,
        resourceSize,

        // Root Directory nằm tại offset tương đối 0.
        0,

        // Depth của root.
        0,

        activeDirectories,
        totalEntries,
        totalDataEntries
    );

    wcout
        << L"\n[+] Total Resource Entries : "
        << dec
        << totalEntries
        << L'\n'

        << L"[+] Total Data Entries     : "
        << totalDataEntries
        << L'\n';

    wcout
        << dec
        << setfill(L' ');
}

// Đổi mã Relocation Type thành tên dễ đọc.
const wchar_t* getRelocationTypeName(
    WORD relocationType)
{
    switch (relocationType)
    {
    case IMAGE_REL_BASED_ABSOLUTE:
        return L"ABSOLUTE (Padding)";

    case IMAGE_REL_BASED_HIGH:
        return L"HIGH";

    case IMAGE_REL_BASED_LOW:
        return L"LOW";

    case IMAGE_REL_BASED_HIGHLOW:
        return L"HIGHLOW (PE32)";

    case IMAGE_REL_BASED_HIGHADJ:
        return L"HIGHADJ";

    case IMAGE_REL_BASED_DIR64:
        return L"DIR64 (PE32+)";

    default:
        return L"Unknown / Machine-specific";
    }
}

// Lấy số byte tại địa chỉ cần được loader điều chỉnh.
size_t getRelocationPatchSize(
    WORD relocationType)
{
    switch (relocationType)
    {
    case IMAGE_REL_BASED_ABSOLUTE:
        // ABSOLUTE chỉ là entry căn chỉnh.
        return 0;

    case IMAGE_REL_BASED_HIGH:
    case IMAGE_REL_BASED_LOW:
    case IMAGE_REL_BASED_HIGHADJ:
        return sizeof(WORD);

    case IMAGE_REL_BASED_HIGHLOW:
        return sizeof(DWORD);

    case IMAGE_REL_BASED_DIR64:
        return sizeof(ULONGLONG);

    default:
        /*
         * Loại phụ thuộc kiến trúc CPU.
         * Dùng 1 byte để kiểm tra RVA có ánh xạ vào file hay không.
         */
        return 1;
    }
}

// Đọc và hiển thị Base Relocation Directory.
void printBaseRelocationDirectory(
    const vector<uint8_t>& fileData,
    const ParsedOptionalHeader& optionalHeader,
    const vector<IMAGE_SECTION_HEADER>& sections)
{
    wcout
        << L"\n┌──────────────────────────────────────────────────────────┐\n"
        << L"│                BASE RELOCATION DIRECTORY                 │\n"
        << L"└──────────────────────────────────────────────────────────┘\n";

    IMAGE_DATA_DIRECTORY relocationData{};

    /*
     * Lấy Data Directory có index
     * IMAGE_DIRECTORY_ENTRY_BASERELOC.
     */
    if (!getDataDirectory(
        optionalHeader,
        IMAGE_DIRECTORY_ENTRY_BASERELOC,
        relocationData))
    {
        wcout
            << L"[!] Không thể lấy Base Relocation Directory.\n";

        return;
    }

    /*
     * RVA hoặc Size bằng 0 nghĩa là directory không tồn tại.
     */
    if (relocationData.VirtualAddress == 0 ||
        relocationData.Size == 0)
    {
        wcout
            << L"[!] File không có Base Relocation Directory.\n";

        return;
    }

    /*
     * Directory phải chứa được ít nhất một
     * IMAGE_BASE_RELOCATION.
     */
    if (relocationData.Size <
        sizeof(IMAGE_BASE_RELOCATION))
    {
        wcout
            << L"[!] Base Relocation Directory quá nhỏ.\n";

        return;
    }

    const DWORD sizeOfHeaders =
        getSizeOfHeaders(optionalHeader);

    size_t relocationDirectoryOffset = 0;

    /*
     * Đổi RVA đầu Relocation Directory
     * thành File Offset.
     */
    if (!rvaToFileOffset(
        relocationData.VirtualAddress,
        sizeof(IMAGE_BASE_RELOCATION),
        sections,
        sizeOfHeaders,
        fileData.size(),
        relocationDirectoryOffset))
    {
        wcout
            << L"[!] Base Relocation Directory RVA "
            << L"không hợp lệ.\n";

        return;
    }

    const size_t relocationDirectorySize =
        static_cast<size_t>(relocationData.Size);

    /*
     * Kiểm tra toàn bộ vùng Relocation Directory
     * có nằm trong file không.
     */
    if (!isRangeValid(
        relocationDirectoryOffset,
        relocationDirectorySize,
        fileData.size()))
    {
        wcout
            << L"[!] Vùng Base Relocation Directory "
            << L"nằm ngoài phạm vi file.\n";

        return;
    }

    wcout
        << hex
        << uppercase
        << setfill(L'0')

        << L"[+] RVA         : 0x"
        << setw(8)
        << relocationData.VirtualAddress

        << L"    File Offset : 0x"
        << setw(8)
        << relocationDirectoryOffset

        << L"    Size : 0x"
        << setw(8)
        << relocationData.Size
        << L"\n\n";

    /*
     * Base Relocation Directory gồm nhiều block:
     *
     * IMAGE_BASE_RELOCATION
     * ├── VirtualAddress: RVA đầu page
     * ├── SizeOfBlock
     * └── WORD entries[]
     */
    size_t processedSize = 0;
    size_t totalBlocks = 0;
    size_t totalEntries = 0;
    size_t totalFixups = 0;

    while (processedSize <
        relocationDirectorySize)
    {
        const size_t remainingSize =
            relocationDirectorySize -
            processedSize;

        /*
         * Phần còn lại không đủ chứa header block.
         */
        if (remainingSize <
            sizeof(IMAGE_BASE_RELOCATION))
        {
            wcout
                << L"[!] Còn "
                << dec
                << remainingSize
                << L" byte cuối không đủ chứa "
                << L"IMAGE_BASE_RELOCATION.\n";

            break;
        }

        const size_t blockFileOffset =
            relocationDirectoryOffset +
            processedSize;

        IMAGE_BASE_RELOCATION block{};

        memcpy(
            &block,
            fileData.data() + blockFileOffset,
            sizeof(IMAGE_BASE_RELOCATION)
        );

        /*
         * Một vùng toàn số 0 có thể là padding ở cuối.
         * Phải dừng để tránh vòng lặp vô hạn.
         */
        if (block.VirtualAddress == 0 &&
            block.SizeOfBlock == 0)
        {
            wcout
                << L"[+] Gặp vùng padding ở cuối "
                << L"Relocation Directory.\n";

            break;
        }

        /*
         * SizeOfBlock phải chứa được header.
         */
        if (block.SizeOfBlock <
            sizeof(IMAGE_BASE_RELOCATION))
        {
            wcout
                << L"[!] SizeOfBlock không hợp lệ tại "
                << L"File Offset 0x"
                << hex
                << uppercase
                << blockFileOffset
                << L".\n";

            break;
        }

        const size_t blockSize =
            static_cast<size_t>(
                block.SizeOfBlock
                );

        /*
         * Block không được vượt ra khỏi
         * kích thước Data Directory.
         */
        if (blockSize > remainingSize)
        {
            wcout
                << L"[!] Relocation Block vượt khỏi "
                << L"Data Directory.\n";

            break;
        }

        const size_t entriesByteSize =
            blockSize -
            sizeof(IMAGE_BASE_RELOCATION);

        /*
         * Mỗi entry có kích thước WORD = 2 byte.
         */
        if (entriesByteSize %
            sizeof(WORD) != 0)
        {
            wcout
                << L"[!] Kích thước bảng Relocation Entry "
                << L"không hợp lệ.\n";

            break;
        }

        const size_t entryCount =
            entriesByteSize /
            sizeof(WORD);

        wcout
            << L"┌─ Block ["
            << dec
            << totalBlocks
            << L"]\n"

            << hex
            << uppercase
            << setfill(L'0')

            << L"│  Block File Offset : 0x"
            << setw(8)
            << blockFileOffset
            << L'\n'

            << L"│  Page RVA          : 0x"
            << setw(8)
            << block.VirtualAddress
            << L'\n'

            << L"│  SizeOfBlock       : 0x"
            << setw(8)
            << block.SizeOfBlock
            << L'\n'

            << L"│  Entry Count       : "
            << dec
            << entryCount
            << L"\n";

        /*
         * Danh sách WORD entry nằm ngay sau
         * IMAGE_BASE_RELOCATION.
         */
        const size_t entriesFileOffset =
            blockFileOffset +
            sizeof(IMAGE_BASE_RELOCATION);

        for (size_t i = 0;
            i < entryCount;
            ++i)
        {
            const size_t entryFileOffset =
                entriesFileOffset +
                i * sizeof(WORD);

            WORD rawEntry = 0;

            memcpy(
                &rawEntry,
                fileData.data() + entryFileOffset,
                sizeof(WORD)
            );

            /*
             * 4 bit cao là Type.
             */
            const WORD relocationType =
                static_cast<WORD>(
                    rawEntry >> 12
                    );

            /*
             * 12 bit thấp là Offset trong page.
             */
            const WORD offsetInPage =
                static_cast<WORD>(
                    rawEntry & 0x0FFF
                    );

            ++totalEntries;

            wcout
                << L"│\n"
                << L"│  Entry ["
                << dec
                << i
                << L"]\n"

                << hex
                << uppercase
                << setfill(L'0')

                << L"│      Entry File Offset : 0x"
                << setw(8)
                << entryFileOffset
                << L'\n'

                << L"│      Raw Entry         : 0x"
                << setw(4)
                << static_cast<unsigned int>(
                    rawEntry
                    )
                << L'\n'

                << L"│      Type              : "
                << dec
                << static_cast<unsigned int>(
                    relocationType
                    )
                << L" ("
                << getRelocationTypeName(
                    relocationType
                )
                << L")\n"

                << hex
                << uppercase
                << setfill(L'0')

                << L"│      Offset In Page    : 0x"
                << setw(3)
                << static_cast<unsigned int>(
                    offsetInPage
                    )
                << L'\n';

            /*
             * ABSOLUTE không phải địa chỉ cần sửa.
             * Nó chỉ dùng để padding/căn chỉnh block.
             */
            if (relocationType ==
                IMAGE_REL_BASED_ABSOLUTE)
            {
                wcout
                    << L"│      Target            : "
                    << L"(Padding, không cần relocation)\n";

                continue;
            }

            ++totalFixups;

            /*
             * Target RVA =
             * Page RVA + Offset trong page.
             */
            const uint64_t targetRva64 =
                static_cast<uint64_t>(
                    block.VirtualAddress
                    ) +
                offsetInPage;

            if (targetRva64 >
                numeric_limits<DWORD>::max())
            {
                wcout
                    << L"│      Target RVA        : "
                    << L"(Overflow)\n";

                continue;
            }

            const DWORD targetRva =
                static_cast<DWORD>(
                    targetRva64
                    );

            const size_t patchSize =
                getRelocationPatchSize(
                    relocationType
                );

            size_t targetFileOffset = 0;

            /*
             * Kiểm tra RVA của vị trí cần relocation
             * có ánh xạ đến dữ liệu raw trong file không.
             */
            const bool validTarget =
                rvaToFileOffset(
                    targetRva,
                    patchSize,
                    sections,
                    sizeOfHeaders,
                    fileData.size(),
                    targetFileOffset
                );

            const ULONGLONG targetVa =
                getImageBase(optionalHeader) +
                targetRva;

            wcout
                << hex
                << uppercase
                << setfill(L'0')

                << L"│      Target RVA        : 0x"
                << setw(8)
                << targetRva
                << L'\n'

                << L"│      Preferred VA      : 0x"
                << setw(16)
                << static_cast<unsigned long long>(
                    targetVa
                    )
                << L'\n'

                << L"│      Patch Size        : 0x"
                << setw(2)
                << patchSize
                << L" byte\n";

            if (validTarget)
            {
                wcout
                    << L"│      Target File Offset: 0x"
                    << setw(8)
                    << targetFileOffset
                    << L'\n';
            }
            else
            {
                wcout
                    << L"│      Target File Offset: "
                    << L"(Không có dữ liệu raw tương ứng)\n";
            }
        }

        wcout
            << L"└──────────────────────────────────────────────────────────\n\n";

        /*
         * Chuyển đến block kế tiếp.
         */
        processedSize += blockSize;
        ++totalBlocks;
    }

    wcout
        << L"[+] Total Relocation Blocks  : "
        << dec
        << totalBlocks
        << L'\n'

        << L"[+] Total Raw Entries        : "
        << totalEntries
        << L'\n'

        << L"[+] Total Relocation Fixups  : "
        << totalFixups
        << L'\n';

    wcout
        << dec
        << setfill(L' ');
}
int wmain(int argc, wchar_t* argv[])
{
    // Cho phép console nhập và hiển thị Unicode.
    _setmode(_fileno(stdin), _O_U16TEXT);
    _setmode(_fileno(stdout), _O_U16TEXT);
    _setmode(_fileno(stderr), _O_U16TEXT);

    wstring filePath;

    // Có truyền đường dẫn từ command line.
    if (argc >= 2)
    {
        filePath = argv[1];
    }
    else
    {
        wcout << L"Nhập đường dẫn file PE: ";
        getline(wcin, filePath);
    }

    filePath = removeQuotes(filePath);

    if (filePath.empty())
    {
        wcerr << L"Đường dẫn không được để trống.\n";
        return 1;
    }

    vector<uint8_t> fileData;

    if (!loadFile(filePath, fileData))
    {
        return 1;
    }

    wcout
        << L"\nĐọc file thành công.\n"
        << L"Đường dẫn: " << filePath << L'\n'
        << L"Kích thước: " << fileData.size()
        << L" byte\n";

    //Hiển thị DOS HEADER
    IMAGE_DOS_HEADER dosHeader{};
    size_t ntHeaderOffset = 0; 

    if (!parseDosHeader(
            fileData,
            dosHeader,
            ntHeaderOffset))
    {
        return 1;
    }

    wcout 
        << L"\nFile PE hợp lệ.\n";

    printDosHeader(
        dosHeader,
        ntHeaderOffset);

    IMAGE_FILE_HEADER fileHeader{};
    size_t fileHeaderOffset = 0;

    if (!parseFileHeader(
            fileData,
            ntHeaderOffset,
            fileHeader,
            fileHeaderOffset))
    {
        return 1;
    }

    printFileHeader(
        fileHeader,
        fileHeaderOffset);


    ParsedOptionalHeader optionalHeader{};

    if (!parseOptionalHeader(
            fileData,
            fileHeader,
            fileHeaderOffset,
            optionalHeader))
    {
        return 1;
    }

    printOptionalHeader(
        optionalHeader,
        fileHeader
    );

    printDataDirectories(
        optionalHeader
    );

    vector<IMAGE_SECTION_HEADER> sections;
    size_t sectionTableOffset = 0;

    if (!parseSectionHeaders(
            fileData,
            fileHeader,
            optionalHeader,
            sections,
            sectionTableOffset))
    {
        return 1;
    }

    printSectionHeaders(
        sections,
        sectionTableOffset);

    printExportDirectory(
        fileData,
        optionalHeader,
        sections);

    printImportDirectory(
        fileData,
        optionalHeader,
        sections);

    printResourceDirectory(
        fileData,
        optionalHeader,
        sections);

    printBaseRelocationDirectory(
        fileData,
        optionalHeader,
        sections);

    const DWORD entryPointRva =
        getEntryPointRva(optionalHeader);

    printRvaMapping(
        L"ENTRY POINT ADDRESS",
        entryPointRva,
        optionalHeader,
        sections,
        fileData.size());

    return 0;
}