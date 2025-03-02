#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace ldb {
using Byte64 = std::array<std::byte, 8>;
using Byte128 = std::array<std::byte, 16>;

class VirtAddr {
 public:
  VirtAddr() = default;
  explicit VirtAddr(std::uint64_t addr) : addr_{addr} {}

  std::uint64_t addr() const { return addr_; }

  VirtAddr operator+(std::uint64_t offset) const {
    return VirtAddr{addr_ + offset};
  }

  VirtAddr operator-(std::uint64_t offset) const {
    return VirtAddr{addr_ - offset};
  }

  VirtAddr operator+=(std::uint64_t offset) {
    addr_ += offset;
    return *this;
  }

  VirtAddr operator-=(std::uint64_t offset) {
    addr_ -= offset;
    return *this;
  }

  bool operator==(const VirtAddr& other) const { return addr_ == other.addr_; }

  bool operator!=(const VirtAddr& other) const { return addr_ != other.addr_; }

  bool operator<(const VirtAddr& other) const { return addr_ < other.addr_; }

  bool operator>(const VirtAddr& other) const { return addr_ > other.addr_; }

  bool operator<=(const VirtAddr& other) const { return addr_ <= other.addr_; }

  bool operator>=(const VirtAddr& other) const { return addr_ >= other.addr_; }

 private:
  std::uint64_t addr_ = 0;
};
}  // namespace ldb