#pragma once

#include "PE/PeTypes.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace ScanEngineInternal
{
    class PeReader
    {
    public:
        PeParseStatus Open(const std::wstring& path, DWORD& win32Error);
        const PeImage& Image() const noexcept { return image_; }

        bool RvaToFileOffset(
            std::uint32_t rva,
            std::size_t requiredSize,
            std::size_t& fileOffset) const noexcept;
        bool ReadCStringRva(std::uint32_t rva, std::string& value, std::size_t maxLength = 4096) const;

    private:
        bool IsRangeValid(std::size_t offset, std::size_t size) const noexcept;
        bool ReadU16(std::size_t offset, std::uint16_t& value) const noexcept;
        bool ReadU32(std::size_t offset, std::uint32_t& value) const noexcept;
        bool ReadU64(std::size_t offset, std::uint64_t& value) const noexcept;
        double SectionEntropy(const PeSection& section) const;

        PeImage image_{};
    };
}
