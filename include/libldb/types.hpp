#ifndef LDB_TYPES_HPP
#define LDB_TYPES_HPP

#include <array>
#include <cstddef>

namespace ldb {
using byte64 = std::array<std::byte, 8>;
using byte128 = std::array<std::byte, 16>;
} // namespace ldb

#endif