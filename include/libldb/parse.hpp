#pragma once

#include <charconv>
#include <cstdint>
#include <optional>
#include <string_view>

namespace ldb {
template <typename I>
  requires std::integral<I> || std::same_as<I, std::byte>
std::optional<I> ToIntegral(std::string_view sv, int base = 10) {
  auto begin = sv.begin();
  if (base == 16 && sv.size() > 1 && begin[0] == '0' && begin[1] == 'x') {
    begin += 2;
  }
  if constexpr (std::same_as<I, std::byte>) {
    std::uint8_t value;
    auto [ptr, ec] = std::from_chars(begin, sv.end(), value, base);
    if (ptr != sv.end()) return {};
    return static_cast<std::byte>(value);
  } else {
    I ret;
    auto [ptr, ec] = std::from_chars(begin, sv.end(), ret, base);
    if (ptr != sv.end()) return {};
    return ret;
  }
}

template <std::floating_point F>
std::optional<F> ToFloat(std::string_view sv) {
  F ret;
  auto [ptr, ec] = std::from_chars(sv.begin(), sv.end(), ret);
  if (ptr != sv.end()) return {};
  return ret;
}

template <std::size_t N>
auto ParseVector(std::string_view text) {
  auto invalid = [] { ldb::Error::Send("Invalid format"); };
  std::array<std::byte, N> bytes;
  auto c = text.data();
  // [0x12,0x34,0x56]
  if (*c++ != '[') invalid();
  for (int i = 0; i < N - 1; i++) {
    bytes[i] = ToIntegral<std::byte>({c, 4}, 16).value();
    c += 4;
    if (*c++ != ',') invalid();
  }
  bytes[N - 1] = ToIntegral<std::byte>({c, 4}, 16).value();
  c += 4;
  if (*c++ != ']') invalid();
  if (c != text.end()) invalid();
  return bytes;
}

// [0xff,0xff]
inline auto ParseVector(std::string_view text) {
  auto invalid = [] { ldb::Error::Send("Invalid format"); };
  std::vector<std::byte> bytes;
  auto c = text.data();
  if (*c++ != '[') invalid();
  while (*c != ']') {
    auto byte = ldb::ToIntegral<std::byte>({c, 4}, 16);
    bytes.push_back(byte.value());
    c += 4;
    if (*c == ',')
      c++;
    else if (*c != ']')
      invalid();
  }
  if (++c != text.end()) invalid();
  return bytes;
}
}  // namespace ldb