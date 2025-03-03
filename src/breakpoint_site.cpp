#include <sys/ptrace.h>

#include <cerrno>
#include <libldb/breakpoint_site.hpp>
#include <libldb/process.hpp>
namespace {
// Get the next ID for the breakpoint site.
auto GetNextId() {
  static ldb::BreakpointSite::IdType id = 0;
  return ++id;
}
}  // namespace

ldb::BreakpointSite::BreakpointSite(Process& process, VirtAddr address)
    : process_{&process}, address_{address}, is_enabled_{false}, saved_data_{} {
  id_ = GetNextId();
}

void ldb::BreakpointSite::Enable() {
  if (is_enabled_) {
    return;
  }

  errno = 0;
  // Get the original instruction.
  std::uint64_t data =
      ptrace(PTRACE_PEEKDATA, process_->pid(), address_, nullptr);
  if (errno != 0) {
    Error::SendErrno("Enabling breakpoint site failed");
  }
  // Save the original instruction.
  saved_data_ = static_cast<std::byte>(data & 0xff);
  // Replace the instruction at the breakpoint site with the int3 instruction.
  std::uint64_t int3 = 0xcc;
  std::uint64_t data_with_int3 = (data & ~0xff) | int3;
  if (ptrace(PTRACE_POKEDATA, process_->pid(), address_, data_with_int3) < 0) {
    Error::SendErrno("Enabling breakpoint site failed");
  }
  is_enabled_ = true;
}

void ldb::BreakpointSite::Disable() {
  if (!is_enabled_) {
    return;
  }

  errno = 0;
  std::uint64_t data =
      ptrace(PTRACE_PEEKDATA, process_->pid(), address_, nullptr);
  if (errno != 0) {
    Error::Send("Disabling breakpoint site failed");
  }

  auto restored_data = (data & ~0xff) | static_cast<std::uint8_t>(saved_data_);
  if (ptrace(PTRACE_POKEDATA, process_->pid(), address_, restored_data) < 0) {
    Error::SendErrno("Disabling breakpoint site failed");
  }
  is_enabled_ = false;
}