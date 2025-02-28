#pragma once

#include <cstddef>
#include <cstring>

#include "libldb/types.hpp"

namespace ldb {
// Convert the given bytes to the given type.
template <typename To>
To FromBytes(const std::byte* bytes) {
  To ret;
  std::memcpy(&ret, bytes, sizeof(To));
  return ret;
}

// Return the bytes of the given value.
template <typename From>
std::byte* AsBytes(From& from) {
  return reinterpret_cast<std::byte*>(&from);
}

// Return the bytes of the given value.
template <typename From>
const std::byte* AsBytes(const From& from) {
  return reinterpret_cast<const std::byte*>(&from);
}

// Convert the given value to a Byte128.
template <typename From>
Byte128 ToByte128(From src) {
  Byte128 ret{};
  std::memcpy(&ret, &src, sizeof(From));
  return ret;
}

// Convert the given value to a Byte64.
template <typename From>
Byte64 ToByte64(From src) {
  Byte64 ret{};
  std::memcpy(&ret, &src, sizeof(From));
  return ret;
}

}  // namespace ldb