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
  Watchpoint(Watchpoint&&) = delete;
  Watchpoint& operator=(Watchpoint&&) = delete;

  using IdType = std::int32_t;
  IdType Id() const { return id; }

  void Enable();
  void Disable();

  bool IsEnabled() const { return isEnabled; }

  VirtAddr Address() const { return address; }

  bool AtAddress(VirtAddr addr) const { return address == addr; }

  bool InRange(VirtAddr low, VirtAddr high) const {
    return low <= address && address < high;
  }

  std::size_t Size() const { return size; }
  StoppointMode Mode() const { return mode; }

  std::uint64_t Data() const { return data; }
  std::uint64_t PreviousData() const { return previousData; }
  void UpdateData();

 private:
  Watchpoint(Process& proc, VirtAddr addr, StoppointMode mode,
             std::size_t size);
  friend Process;

  IdType id;
  Process* process;
  VirtAddr address;
  bool isEnabled;
  StoppointMode mode;
  std::size_t size;
  // index of the debug register a hardware breakpoint is using
  int hardwareRegisterIndex = -1;
  std::uint64_t data = 0;
  std::uint64_t previousData = 0;
};
}  // namespace ldb