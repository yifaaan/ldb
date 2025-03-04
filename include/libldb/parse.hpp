#pragma once

#include <charconv>
#include <cstdint>
#include <libldb/error.hpp>
#include <optional>
#include <string_view>
#include <vector>

namespace ldb {
// Convert a string_view to an integral type.
template <typename I>
std::optional<I> ToIntegral(std::string_view sv, int base = 10) {
  auto begin = std::begin(sv);
  if (base == 16 && sv.size() > 1 && begin[0] == '0' && begin[1] == 'x') {
    begin += 2;
  }

  I ret;
  auto [ptr, ec] = std::from_chars(begin, std::end(sv), ret, base);
  if (ptr != std::end(sv)) {
    return std::nullopt;
  }
  return ret;
}

// Because std::byte is not an integral type, we need to specialize the
// template.
template <>
inline std::optional<std::byte> ToIntegral<std::byte>(std::string_view sv,
                                                      int base) {
  auto uint8 = ToIntegral<std::uint8_t>(sv, base);
  if (uint8) {
    return static_cast<std::byte>(*uint8);
  }
  return std::nullopt;
}

// Convert a string_view to a floating-point type.
template <typename F>
std::optional<F> ToFloat(std::string_view sv) {
  F ret;
  auto [ptr, ec] = std::from_chars(std::begin(sv), std::end(sv), ret);
  if (ptr != std::end(sv)) {
    return std::nullopt;
  }
  return ret;
}

template <std::size_t N>
auto ParseVector(std::string_view text) {
  auto invalid = [] { ldb::Error::Send("Invalid format"); };

  std::array<std::byte, N> bytes;
  auto c = text.data();
  if (*c++ != '[') {
    invalid();
  }
  for (int i = 0; i < N - 1; i++) {
    bytes[i] = ToIntegral<std::byte>({c, 4}, 16).value();
    c += 4;
    if (*c++ != ',') {
      invalid();
    }
  }
  bytes[N - 1] = ToIntegral<std::byte>({c, 4}, 16).value();
  c += 4;

  if (*c++ != ']') {
    invalid();
  }
  if (c != std::end(text)) {
    invalid();
  }
  return bytes;
}
// mem write 0x555555555156 [0xff,0xff]
inline auto ParseVector(std::string_view text) {
  auto invalid = [] { ldb::Error::Send("Invalid format"); };

  std::vector<std::byte> bytes;
  auto c = text.data();

  if (*c++ != '[') {
    invalid();
  }
  while (*c != ']') {
    auto byte = ToIntegral<std::byte>({c, 4}, 16);
    bytes.push_back(*byte);
    c += 4;

    if (*c == ',') {
      c++;
    } else if (*c != ']') {
      invalid();
    }
  }
  if (++c != text.end()) {
    invalid();
  }
  return bytes;
}
}  // namespace ldb