#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

namespace ldb {
using Byte64 = std::array<std::byte, 8>;
using Byte128 = std::array<std::byte, 16>;

class FileAddr;
class Elf;
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

  // Translate the virtual address to a file address.
  FileAddr ToFileAddr(const Elf& elf) const;

 private:
  std::uint64_t addr_ = 0;
};

// 断点类型
// 写入断点
// 读写断点
// 执行断点
enum class StoppointMode {
  Write,
  ReadWrite,
  Execute,
};

// 相对于运行时加载地址的偏移量
class FileAddr {
 public:
  FileAddr() = default;
  FileAddr(const Elf& elf, std::uint64_t addr) : elf_{&elf}, addr_{addr} {}

  FileAddr operator+(std::int64_t offset) const {
    return FileAddr{*elf_, addr_ + offset};
  }

  FileAddr operator-(std::int64_t offset) const {
    return FileAddr{*elf_, addr_ - offset};
  }

  FileAddr operator+=(std::int64_t offset) {
    addr_ += offset;
    return *this;
  }

  FileAddr operator-=(std::int64_t offset) {
    addr_ -= offset;
    return *this;
  }

  bool operator==(const FileAddr& other) const {
    return elf_ == other.elf_ && addr_ == other.addr_;
  }

  bool operator!=(const FileAddr& other) const { return !(*this == other); }

  bool operator<(const FileAddr& other) const {
    assert(elf_ == other.elf_);
    return addr_ < other.addr_;
  }

  bool operator>(const FileAddr& other) const {
    assert(elf_ == other.elf_);
    return addr_ > other.addr_;
  }

  bool operator<=(const FileAddr& other) const {
    assert(elf_ == other.elf_);
    return addr_ <= other.addr_;
  }

  bool operator>=(const FileAddr& other) const {
    assert(elf_ == other.elf_);
    return addr_ >= other.addr_;
  }

  std::uint64_t addr() const { return addr_; }

  const Elf* elf() const { return elf_; }

  VirtAddr ToVirtAddr() const;

 private:
  const Elf* elf_ = nullptr;
  std::uint64_t addr_ = 0;
};

class FileOffset {
 public:
  FileOffset() = default;
  FileOffset(const Elf& elf, std::uint64_t offset)
      : elf_{&elf}, offset_{offset} {}

  std::uint64_t offset() const { return offset_; }

  const Elf* elf() const { return elf_; }

 private:
  const Elf* elf_ = nullptr;
  std::uint64_t offset_ = 0;
};

}  // namespace ldb