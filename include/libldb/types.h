#pragma once

#include <array>
#include <cstddef>

namespace ldb
{
    using Byte64 = std::array<std::byte, 8>;
    using Byte128 = std::array<std::byte, 16>;
} // namespace ldb