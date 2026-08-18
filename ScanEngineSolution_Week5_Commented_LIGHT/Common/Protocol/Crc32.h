#pragma once

#include <cstddef>
#include <cstdint>

namespace AvProtocol
{
    std::uint32_t CalculateCrc32(const void* data, std::size_t size) noexcept;
}
