#pragma once

#include <cstddef>
#include <cstdint>
#include <libldb/types.hpp>

namespace ldb {
class Process;

// 物理断点（physical breakpoint）：
// 这是硬件级别的断点实现
// 真正在内存中修改的指令位置
// 一对多的关系
// 一个逻辑断点可能对应多个物理断点位置
// 如果用户在C++的to_string函数上设置断点
// 由于C++支持函数重载，to_string可能有多个不同参数类型的版本
// 每个重载版本都位于不同的内存地址
// 因此一个逻辑断点需要创建多个物理断点
class BreakpointSite {
 public:
  BreakpointSite() = delete;
  BreakpointSite(const BreakpointSite&) = delete;
  BreakpointSite& operator=(const BreakpointSite&) = delete;

  using IdType = std::int32_t;
  // Get the ID of the breakpoint site.
  IdType id() const { return id_; }

  // Enable the breakpoint site.
  void Enable();
  // Disable the breakpoint site.
  void Disable();

  // Check if the breakpoint site is enabled.
  bool IsEnabled() const { return is_enabled_; }

  // Get the address of the breakpoint site.
  VirtAddr address() const { return address_; }

  // Check if the breakpoint site is at the specified address
  bool AtAddress(VirtAddr address) const { return address_ == address; }

  // Check if the breakpoint site is within the range of [low, high).
  bool InRange(VirtAddr low, VirtAddr high) const {
    return low <= address_ && address_ < high;
  }

 private:
  BreakpointSite(Process& process, VirtAddr address);
  friend Process;

  IdType id_;
  Process* process_;
  VirtAddr address_;
  bool is_enabled_;
  // Hold the data we replace with the int3 instruction when setting a
  // breakpoint.
  std::byte saved_data_;
};
}  // namespace ldb