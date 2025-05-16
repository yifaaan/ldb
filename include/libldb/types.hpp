#pragma once

#include <array>
#include <cstddef>

namespace ldb
{
    /// @brief 64 位字节数组
    using byte64 = std::array<std::byte, 8>;
    /// @brief 128 位字节数组
    using byte128 = std::array<std::byte, 16>;
}