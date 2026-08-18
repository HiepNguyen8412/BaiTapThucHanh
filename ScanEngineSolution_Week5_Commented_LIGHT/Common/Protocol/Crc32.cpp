#include "Protocol/Crc32.h"

//Checcksum 
namespace AvProtocol
{
    // Tinh CRC32 cho payload. Ben nhan tinh lai va so sanh voi checksum trong header;
    // neu khac nhau thi frame bi loi/hong va se bi tu choi.
    std::uint32_t CalculateCrc32(const void* data, std::size_t size) noexcept
    {
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        std::uint32_t crc = 0xFFFFFFFFu;
        for (std::size_t i = 0; i < size; ++i)
        {
            crc ^= bytes[i];
            for (int bit = 0; bit < 8; ++bit)
            {
                const std::uint32_t mask = 0u - (crc & 1u);
                crc = (crc >> 1u) ^ (0xEDB88320u & mask);
            }
        }
        return ~crc;
    }
}
