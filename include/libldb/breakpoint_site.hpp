#pragma once

#include <libldb/types.hpp>

namespace ldb {
class Breakpoint;
class Process;
class BreakpointSite {
 public:
  BreakpointSite() = delete;
  BreakpointSite(const BreakpointSite&) = delete;
  BreakpointSite& operator=(const BreakpointSite&) = delete;
  BreakpointSite(BreakpointSite&&) = delete;
  BreakpointSite& operator=(BreakpointSite&&) = delete;

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

  bool IsHardware() const { return isHardware; }
  bool IsInternal() const { return isInternal; }

 private:
  BreakpointSite(Process& proc, VirtAddr addr, bool _isHardware = false,
                 bool _isInternal = false);
  BreakpointSite(Breakpoint* _parent, IdType _id, Process& proc, VirtAddr addr,
                 bool _isHardware = false, bool _isInternal = false);
  friend Process;

  IdType id;
  Process* process;
  VirtAddr address;
  bool isEnabled;
  std::byte savedData{};
  bool isHardware;
  // debugger use it to implement functionality like source-level stepping and
  // shared library tracing
  bool isInternal;
  // index of the debug register a hardware breakpoint is using
  int hardwareRegisterIndex = -1;
  Breakpoint* parent = nullptr;
};
}  // namespace ldb