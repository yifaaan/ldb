#ifndef LDB_BIT_HPP
#define LDB_BIT_HPP

#include "libldb/types.hpp"
#include <cstddef>
#include <cstring>

namespace ldb {

/// Constructs an object of the given type.
template <typename To> To from_bytes(const std::byte* bytes) {
  To ret;
  std::memcpy(&ret, bytes, sizeof(To));
  return ret;
}
/// Casts its argument into a pointer to std::bytes.
template <typename From> const std::byte* as_bytes(const From& from) {
  return reinterpret_cast<const std::byte*>(&from);
}

template <typename From> byte128 to_byte128(From src) {
  // Initialize all elements to 0 rather than leaving them uninitialized.
  byte128 ret{};
  std::memcpy(&ret, &src, sizeof(From));
  return ret;
}

template <typename From> byte64 to_byte64(From src) {
  byte64 ret{};
  std::memcpy(&ret, &src, sizeof(From));
  return ret;
}
} // namespace ldb
#endif