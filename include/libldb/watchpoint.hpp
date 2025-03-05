#pragma once

#include <cstdint>
#include <libldb/types.hpp>

namespace ldb {
class Process;
class Watchpoint {
 public:
  Watchpoint() = delete;
  Watchpoint(const Watchpoint&) = delete;
  Watchpoint& operator=(const Watchpoint&) = delete;

  using IdType = std::int32_t;
  IdType id() const { return id_; }

  void Enable();
  void Disable();
  bool IsEnabled() const { return is_enabled_; }

  VirtAddr address() const { return address_; }

  StoppointMode mode() const { return mode_; }

  std::size_t size() const { return size_; }

  bool AtAddress(VirtAddr address) const { return address_ == address; }

  bool InRange(VirtAddr low, VirtAddr high) const {
    return low <= address_ && address_ < high;
  }

  std::uint64_t data() const { return data_;}

  std::uint64_t previous_data() const { return previous_data_; }

  // Update the data at the watchpoint address.
  void UpdateData();

 private:
  friend Process;
  Watchpoint(Process& process, VirtAddr address, StoppointMode mode,
             std::size_t size);

  IdType id_;
  Process* process_;
  VirtAddr address_;
  StoppointMode mode_;
  std::size_t size_;
  bool is_enabled_;
  int hardware_register_index_ = -1;
  // Current data at the watchpoint address.
  std::uint64_t data_ = 0;
  // Previous data at the watchpoint address.
  std::uint64_t previous_data_ = 0;
};
}  // namespace ldb