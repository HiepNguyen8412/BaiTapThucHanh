#include "PE/PeReader.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <set>

namespace
{
    constexpr std::uint16_t DOS_MAGIC = 0x5A4D; // MZ
    constexpr std::uint32_t NT_MAGIC = 0x00004550; // PE\0\0
    constexpr std::uint16_t PE32_MAGIC = 0x10B;
    constexpr std::uint16_t PE32_PLUS_MAGIC = 0x20B;
    constexpr std::uint16_t FILE_DLL = 0x2000;
    constexpr std::uint32_t MEM_EXECUTE = 0x20000000u;
    constexpr std::uint32_t MEM_WRITE = 0x80000000u;

    constexpr std::size_t DIR_EXPORT = 0;
    constexpr std::size_t DIR_IMPORT = 1;
    constexpr std::size_t DIR_RESOURCE = 2;
    constexpr std::size_t DIR_SECURITY = 4;
    constexpr std::size_t DIR_DEBUG = 6;
    constexpr std::size_t DIR_TLS = 9;
    constexpr std::size_t DIR_DELAY_IMPORT = 13;
    constexpr std::size_t DIR_CLR = 14;

    bool AddWouldOverflow(std::uint64_t a, std::uint64_t b) noexcept
    {
        return b > std::numeric_limits<std::uint64_t>::max() - a;
    }

    bool HasSysExtension(const std::wstring& path)
    {
        const auto dot = path.find_last_of(L'.');
        return dot != std::wstring::npos && _wcsicmp(path.c_str() + dot, L".sys") == 0;
    }
}

namespace ScanEngineInternal
{
    // Kiem tra [offset, offset+size) nam HOAN TOAN trong buffer file.
    // Day la lop phong thu chinh chong PE malformed/truncated gay doc vuot bien.
    bool PeReader::IsRangeValid(std::size_t offset, std::size_t size) const noexcept
    {
        return offset <= image_.bytes.size() && size <= image_.bytes.size() - offset;
    }

    bool PeReader::ReadU16(std::size_t offset, std::uint16_t& value) const noexcept
    {
        if (!IsRangeValid(offset, sizeof(value))) return false;
        std::memcpy(&value, image_.bytes.data() + offset, sizeof(value));
        return true;
    }

    bool PeReader::ReadU32(std::size_t offset, std::uint32_t& value) const noexcept
    {
        if (!IsRangeValid(offset, sizeof(value))) return false;
        std::memcpy(&value, image_.bytes.data() + offset, sizeof(value));
        return true;
    }

    bool PeReader::ReadU64(std::size_t offset, std::uint64_t& value) const noexcept
    {
        if (!IsRangeValid(offset, sizeof(value))) return false;
        std::memcpy(&value, image_.bytes.data() + offset, sizeof(value));
        return true;
    }

    double PeReader::SectionEntropy(const PeSection& section) const
    {
        if (section.sizeOfRawData == 0 ||
            !IsRangeValid(section.pointerToRawData, section.sizeOfRawData)) return 0.0;

        std::array<std::uint64_t, 256> counts{};
        const auto* data = image_.bytes.data() + section.pointerToRawData;
        for (std::size_t i = 0; i < section.sizeOfRawData; ++i) ++counts[data[i]];
        const double total = static_cast<double>(section.sizeOfRawData);
        double entropy = 0.0;
        for (const auto count : counts)
        {
            if (count == 0) continue;
            const double p = static_cast<double>(count) / total;
            entropy -= p * std::log2(p);
        }
        return entropy;
    }

    // Chuyen RVA (dia chi tuong doi khi PE duoc nap) sang offset thuc trong file tren dia.
    // Ham duyet section va validate size de khong tao offset vuot khoi buffer.
    bool PeReader::RvaToFileOffset(
        std::uint32_t rva,
        std::size_t requiredSize,
        std::size_t& fileOffset) const noexcept
    {
        fileOffset = 0;
        if (!image_.isPe) return false;

        if (rva < image_.sizeOfHeaders)
        {
            if (!IsRangeValid(rva, requiredSize)) return false;
            fileOffset = rva;
            return true;
        }

        for (const auto& section : image_.sections)
        {
            const std::uint64_t span = (std::max)(
                static_cast<std::uint64_t>(section.virtualSize),
                static_cast<std::uint64_t>(section.sizeOfRawData));
            const std::uint64_t begin = section.virtualAddress;
            const std::uint64_t end = begin + span;
            if (static_cast<std::uint64_t>(rva) < begin || static_cast<std::uint64_t>(rva) >= end) continue;

            const std::uint64_t delta = static_cast<std::uint64_t>(rva) - begin;
            if (delta >= section.sizeOfRawData) return false; // virtual-only bytes have no file offset.
            const std::uint64_t offset = static_cast<std::uint64_t>(section.pointerToRawData) + delta;
            if (offset > std::numeric_limits<std::size_t>::max()) return false;
            const auto candidate = static_cast<std::size_t>(offset);
            if (!IsRangeValid(candidate, requiredSize)) return false;
            fileOffset = candidate;
            return true;
        }
        return false;
    }

    // Doc chuoi C tu RVA voi maxLength de khong quet vo han neu file thieu byte null ket thuc.
    bool PeReader::ReadCStringRva(std::uint32_t rva, std::string& value, std::size_t maxLength) const
    {
        value.clear();
        std::size_t offset = 0;
        if (!RvaToFileOffset(rva, 1, offset)) return false;
        const std::size_t limit = (std::min)(image_.bytes.size(), offset + maxLength);
        for (std::size_t i = offset; i < limit; ++i)
        {
            const char ch = static_cast<char>(image_.bytes[i]);
            if (ch == '\0') return true;
            value.push_back(ch);
        }
        value.clear();
        return false;
    }

    // PE PARSER CHINH: doc file -> DOS header/MZ -> e_lfanew -> PE signature -> File/Optional headers
    // -> DataDirectory -> Section table, dong thoi kiem tra moi offset/size truoc khi truy cap.
    PeParseStatus PeReader::Open(const std::wstring& path, DWORD& win32Error)
    {
        image_ = {};
        win32Error = ERROR_SUCCESS;

        HANDLE file = CreateFileW(
            path.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            win32Error = GetLastError();
            return PeParseStatus::IoError;
        }

        LARGE_INTEGER size{};
        if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 ||
            static_cast<unsigned long long>(size.QuadPart) > static_cast<unsigned long long>(SIZE_MAX))
        {
            win32Error = GetLastError();
            if (win32Error == ERROR_SUCCESS) win32Error = ERROR_FILE_TOO_LARGE;
            CloseHandle(file);
            return PeParseStatus::IoError;
        }

        image_.bytes.resize(static_cast<std::size_t>(size.QuadPart));
        std::size_t total = 0;
        while (total < image_.bytes.size())
        {
            const DWORD request = static_cast<DWORD>((std::min<std::size_t>)(
                image_.bytes.size() - total, 1024u * 1024u));
            DWORD read = 0;
            if (!ReadFile(file, image_.bytes.data() + total, request, &read, nullptr))
            {
                win32Error = GetLastError();
                CloseHandle(file);
                image_ = {};
                return PeParseStatus::IoError;
            }
            if (read == 0)
            {
                win32Error = ERROR_HANDLE_EOF;
                CloseHandle(file);
                image_ = {};
                return PeParseStatus::IoError;
            }
            total += read;
        }
        CloseHandle(file);

        if (image_.bytes.size() < 2) return PeParseStatus::NotPe;
        std::uint16_t dosMagic = 0;
        ReadU16(0, dosMagic);
        if (dosMagic != DOS_MAGIC) return PeParseStatus::NotPe;
        if (!IsRangeValid(0, 0x40)) return PeParseStatus::MalformedPe;

        std::uint32_t ntOffset = 0;
        if (!ReadU32(0x3C, ntOffset)) return PeParseStatus::MalformedPe;
        if (!IsRangeValid(ntOffset, 4 + 20)) return PeParseStatus::MalformedPe;

        std::uint32_t signature = 0;
        if (!ReadU32(ntOffset, signature) || signature != NT_MAGIC) return PeParseStatus::MalformedPe;
        const std::size_t fileHeader = static_cast<std::size_t>(ntOffset) + 4;
        std::uint16_t optionalSize = 0;
        if (!ReadU16(fileHeader + 0, image_.machine) ||
            !ReadU16(fileHeader + 2, image_.sectionCount) ||
            !ReadU32(fileHeader + 4, image_.timeDateStamp) ||
            !ReadU16(fileHeader + 16, optionalSize) ||
            !ReadU16(fileHeader + 18, image_.characteristics))
        {
            return PeParseStatus::MalformedPe;
        }
        if (image_.sectionCount == 0 || image_.sectionCount > 96) return PeParseStatus::StructCorrupt;

        const std::size_t optional = fileHeader + 20;
        if (!IsRangeValid(optional, optionalSize) || optionalSize < 96) return PeParseStatus::MalformedPe;
        std::uint16_t optionalMagic = 0;
        if (!ReadU16(optional, optionalMagic)) return PeParseStatus::MalformedPe;
        if (optionalMagic == PE32_MAGIC) image_.isPe32Plus = false;
        else if (optionalMagic == PE32_PLUS_MAGIC) image_.isPe32Plus = true;
        else return PeParseStatus::MalformedPe;

        const std::size_t minimumOptional = image_.isPe32Plus ? 112 : 96;
        if (optionalSize < minimumOptional) return PeParseStatus::MalformedPe;

        if (!ReadU32(optional + 16, image_.entryPointRva) ||
            !ReadU32(optional + 32, image_.sectionAlignment) ||
            !ReadU32(optional + 36, image_.fileAlignment) ||
            !ReadU32(optional + 60, image_.sizeOfHeaders) ||
            !ReadU16(optional + 68, image_.subsystem))
        {
            return PeParseStatus::MalformedPe;
        }
        if (image_.isPe32Plus)
        {
            if (!ReadU64(optional + 24, image_.imageBase) ||
                !ReadU32(optional + 108, image_.numberOfRvaAndSizes)) return PeParseStatus::MalformedPe;
        }
        else
        {
            std::uint32_t imageBase32 = 0;
            if (!ReadU32(optional + 28, imageBase32) ||
                !ReadU32(optional + 92, image_.numberOfRvaAndSizes)) return PeParseStatus::MalformedPe;
            image_.imageBase = imageBase32;
        }

        const std::size_t directoryOffset = optional + (image_.isPe32Plus ? 112 : 96);
        const std::uint32_t readableDirectories = (std::min)(image_.numberOfRvaAndSizes, 16u);
        if (image_.numberOfRvaAndSizes > 16) image_.directoryAnomaly = true;
        if (directoryOffset > optional + optionalSize) return PeParseStatus::MalformedPe;
        const std::size_t availableDirectoryBytes = optional + optionalSize - directoryOffset;
        if (static_cast<std::size_t>(readableDirectories) * 8 > availableDirectoryBytes)
        {
            return PeParseStatus::MalformedPe;
        }
        for (std::uint32_t i = 0; i < readableDirectories; ++i)
        {
            if (!ReadU32(directoryOffset + i * 8, image_.directories[i].virtualAddress) ||
                !ReadU32(directoryOffset + i * 8 + 4, image_.directories[i].size))
            {
                return PeParseStatus::MalformedPe;
            }
        }

        const std::size_t sectionTable = optional + optionalSize;
        const std::uint64_t tableBytes = static_cast<std::uint64_t>(image_.sectionCount) * 40u;
        if (tableBytes > SIZE_MAX || !IsRangeValid(sectionTable, static_cast<std::size_t>(tableBytes)))
        {
            return PeParseStatus::StructCorrupt;
        }

        image_.sections.reserve(image_.sectionCount);
        std::uint64_t lastRawEnd = 0;
        for (std::uint16_t i = 0; i < image_.sectionCount; ++i)
        {
            const std::size_t offset = sectionTable + static_cast<std::size_t>(i) * 40;
            PeSection section{};
            char name[9]{};
            std::memcpy(name, image_.bytes.data() + offset, 8);
            std::size_t nameLength = 0;
            while (nameLength < 8 && name[nameLength] != '\0') ++nameLength;
            section.name.assign(name, nameLength);
            if (!ReadU32(offset + 8, section.virtualSize) ||
                !ReadU32(offset + 12, section.virtualAddress) ||
                !ReadU32(offset + 16, section.sizeOfRawData) ||
                !ReadU32(offset + 20, section.pointerToRawData) ||
                !ReadU32(offset + 36, section.characteristics))
            {
                return PeParseStatus::StructCorrupt;
            }

            if (section.sizeOfRawData != 0)
            {
                if (AddWouldOverflow(section.pointerToRawData, section.sizeOfRawData) ||
                    !IsRangeValid(section.pointerToRawData, section.sizeOfRawData))
                {
                    return PeParseStatus::StructCorrupt;
                }
                lastRawEnd = (std::max)(lastRawEnd,
                    static_cast<std::uint64_t>(section.pointerToRawData) + section.sizeOfRawData);
            }
            image_.sections.push_back(section);
        }

        image_.isPe = true;
        image_.isDll = (image_.characteristics & FILE_DLL) != 0;
        image_.isDriver = image_.subsystem == 1 || HasSysExtension(path); // IMAGE_SUBSYSTEM_NATIVE = 1
        image_.isManaged = image_.directories[DIR_CLR].virtualAddress != 0 && image_.directories[DIR_CLR].size != 0;
        image_.isSigned = image_.directories[DIR_SECURITY].virtualAddress != 0 && image_.directories[DIR_SECURITY].size != 0;
        image_.hasDebug = image_.directories[DIR_DEBUG].virtualAddress != 0 && image_.directories[DIR_DEBUG].size != 0;

        const std::size_t richEnd = (std::min<std::size_t>)(ntOffset, image_.bytes.size());
        for (std::size_t i = 0x40; i + 4 <= richEnd; ++i)
        {
            if (std::memcmp(image_.bytes.data() + i, "Rich", 4) == 0)
            {
                image_.hasRichHeader = true;
                break;
            }
        }

        for (auto& section : image_.sections) section.entropy = SectionEntropy(section);
        if (lastRawEnd < image_.bytes.size()) image_.overlaySize = image_.bytes.size() - lastRawEnd;

        // Validate all non-security directories through our own RVA mapper.
        for (std::size_t i = 0; i < image_.directories.size(); ++i)
        {
            const auto& directory = image_.directories[i];
            if (directory.virtualAddress == 0 || directory.size == 0) continue;
            if (i == DIR_SECURITY)
            {
                // PE Security Directory.VirtualAddress is a raw FILE OFFSET, not RVA.
                if (!IsRangeValid(directory.virtualAddress, directory.size))
                {
                    image_.directoryAnomaly = true;
                    return PeParseStatus::StructCorrupt;
                }
                continue;
            }
            std::size_t first = 0;
            if (!RvaToFileOffset(directory.virtualAddress, 1, first))
            {
                image_.directoryAnomaly = true;
                return PeParseStatus::StructCorrupt;
            }
            if (directory.size > 1)
            {
                const std::uint64_t lastRva64 = static_cast<std::uint64_t>(directory.virtualAddress) + directory.size - 1;
                if (lastRva64 > UINT32_MAX)
                {
                    image_.directoryAnomaly = true;
                    return PeParseStatus::StructCorrupt;
                }
                std::size_t last = 0;
                if (!RvaToFileOffset(static_cast<std::uint32_t>(lastRva64), 1, last))
                {
                    image_.directoryAnomaly = true;
                    return PeParseStatus::StructCorrupt;
                }
            }
        }

        return PeParseStatus::Success;
    }
}
