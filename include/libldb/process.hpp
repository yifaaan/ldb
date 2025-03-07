#pragma once

#include <sys/types.h>
#include <sys/user.h>

#include <filesystem>
#include <libldb/bit.hpp>
#include <libldb/breakpoint_site.hpp>
#include <libldb/registers.hpp>
#include <libldb/stoppoint_collection.hpp>
#include <libldb/types.hpp>
#include <libldb/watchpoint.hpp>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace ldb {
enum class ProcessState {
  Stopped,
  Running,
  Exited,
  Terminated,
};

// The reason why the child process stopped (SIGTRAP).
enum class TrapType {
  // ptrace(PTRACE_SINGLESTEP, ...)
  SingleStep,

  // Software breakpoint by int3.
  SoftwareBreak,

  // Hardware breakpoint set by DR0-DR3.
  HardwareBreak,

  // Syscall entry or exit.
  Syscall,

  Unknown,
};

// The tracer to check the arguments to the syscall before it’s executed, then
// check the return value on exit.
struct SyscallInformation {
  // The syscall number.
  std::uint16_t id;

  // Whether the trap is on the syscall entry or exit.
  bool entry;
  union {
    std::array<std::uint64_t, 6> args;
    std::int64_t ret;
  };
};

// The reason why the child process stopped.
struct StopReason {
  StopReason(int wait_status);

  ProcessState reason;
  std::uint8_t info;
  std::optional<TrapType> trap_reason;

  // If the process stopped because of a syscall, this will be the syscall
  // information.
  std::optional<SyscallInformation> syscall_info;
};

class SyscallCatchPolicy {
 public:
  // The policy of catching syscalls.
  enum Mode {
    // Catch no syscall.
    None,
    // Catch only the syscall with the specified number.
    Some,
    // Catch all syscalls.
    All,
  };

  static SyscallCatchPolicy CatchAll() { return {Mode::All, {}}; }

  static SyscallCatchPolicy CatchNone() { return {Mode::None, {}}; }

  static SyscallCatchPolicy CatchSome(std::vector<int> to_catch) {
    return {Mode::Some, to_catch};
  }

  Mode mode() const { return mode_; }

  const std::vector<int>& to_catch() const { return to_catch_; }

 private:
  SyscallCatchPolicy(Mode mode, std::vector<int> to_catch)
      : mode_{mode}, to_catch_{std::move(to_catch)} {}

  Mode mode_ = Mode::None;
  // The syscall numbers to catch.
  std::vector<int> to_catch_;
};

class Process {
 public:
  Process() = delete;
  Process(const Process&) = delete;
  Process& operator=(const Process&) = delete;
  ~Process();

  // Launch a new process and return a pointer to it.
  // When the child pauses, the kernel will send a SIGCHLD signal to parent.
  // If stdout_replacement is provided, the child process's stdout will be
  // replaced with the file descriptor.
  static std::unique_ptr<Process> Launch(
      std::filesystem::path path, bool debug = true,
      std::optional<int> stdout_replacement = std::nullopt);

  // Attach to a running process and return a pointer to it.
  // When the child pauses, the kernel will send a SIGCHLD signal to parent.
  static std::unique_ptr<Process> Attach(pid_t pid);

  // Resume the child process to continue execution.
  void Resume();

  // Wait the child process's change of state.
  // Return the reason why the child process stopped.
  StopReason WaitOnSignal();

  // Augment the stop reason with the trap reason.
  void AugmentStopReason(StopReason& reason);

  pid_t pid() const { return pid_; }

  ProcessState state() const { return state_; }

  // Get the registers of the process.
  Registers& registers() { return *registers_; }
  // Get the registers of the process.
  const Registers& registers() const { return *registers_; }

  // Write all floating point registers.
  // ptrace does not support writing and reading from the x87 area on x64.
  // So we need to write and read all x87 registers.
  void WriteFprs(const user_fpregs_struct& fprs);

  // Write all general purpose registers.
  void WriteGprs(const user_regs_struct& gprs);

  // ptrace provides an area of memory in the same format as the user struct,
  // called the user area, which we can write into to update a single register
  // value.
  void WriteUserArea(std::size_t offset, std::uint64_t value);

  VirtAddr GetPc() const {
    return VirtAddr{registers().ReadByIdAs<std::uint64_t>(RegisterId::rip)};
  }

  // Set the program counter to the given address.
  void SetPc(VirtAddr address) {
    registers().WriteById(RegisterId::rip, address.addr());
  }

  // Create a breakpoint site at the given address.
  BreakpointSite& CreateBreakpointSite(VirtAddr address, bool hardware = false,
                                       bool internal = false);

  // Get the collection of breakpoint sites.
  StoppointCollection<BreakpointSite>& breakpoint_sites() {
    return breakpoint_sites_;
  }
  // Get the collection of breakpoint sites.
  const StoppointCollection<BreakpointSite>& breakpoint_sites() const {
    return breakpoint_sites_;
  }

  // Create a watchpoint at the given address.
  Watchpoint& CreateWatchpoint(VirtAddr address, StoppointMode mode,
                               std::size_t size);

  // Get the collection of watchpoints.
  StoppointCollection<Watchpoint>& watchpoints() { return watchpoints_; }

  // Get the collection of watchpoints.
  const StoppointCollection<Watchpoint>& watchpoints() const {
    return watchpoints_;
  }

  // Step the process to the next instruction.
  ldb::StopReason StepInstruction();

  // Read memory from the process at the given address.
  std::vector<std::byte> ReadMemory(VirtAddr address, std::size_t size) const;

  // Read memory from the process at the given address without trapping on
  // breakpoints.
  std::vector<std::byte> ReadMemoryWithoutTraps(VirtAddr address,
                                                std::size_t size) const;

  // Write memory to the process at the given address.
  void WriteMemory(VirtAddr address, std::span<const std::byte>);

  // Read a value from the process at the given address.
  template <typename T>
  T ReadMemoryAs(VirtAddr address) const {
    auto data = ReadMemory(address, sizeof(T));
    return FromBytes<T>(data.data());
  }

  // Set a hardware breakpoint at the given address. Mode is always Execute.
  int SetHardwareBreakpoint(BreakpointSite::IdType id, VirtAddr address);

  // Set a watchpoint at the given address.
  int SetWatchpoint(Watchpoint::IdType id, VirtAddr address, StoppointMode mode,
                    std::size_t size);

  // Clear a hardware stoppoint at the given index.
  void ClearHardwareStoppoint(int index);

  // Get the current hardware stoppoint.
  // 0 for breakpoint, 1 for watchpoint.
  std::variant<BreakpointSite::IdType, Watchpoint::IdType>
  GetCurrentHardwareStoppoint() const;

  void SetSyscallCatchPolicy(SyscallCatchPolicy policy) {
    syscall_catch_policy_ = std::move(policy);
  }

 private:
  Process(pid_t pid, bool terminate_on_end, bool is_attached)
      : pid_{pid},
        terminate_on_end_{terminate_on_end},
        is_attached_{is_attached},
        registers_{new Registers{*this}} {}

  // Read all the registers of the process to the registers object.
  void ReadAllRegisters();

  // Set a hardware stoppoint at the given address with the given mode and size.
  int SetHardwareStoppoint(VirtAddr address, StoppointMode mode,
                           std::size_t size);

  // Check if we should resume from a syscall stop.
  // If the current syscall is not one we want to trace, resume the process
  // and return the new stop reason. Otherwise return the original reason.
  ldb::StopReason MaybeResumeFromSyscall(const StopReason& reason);

  // Get the auxv of the process.
  // type->value
  std::unordered_map<uint64_t, std::uint64_t> GetAuxv() const;

 private:
  pid_t pid_ = 0;
  bool terminate_on_end_ = true;
  ProcessState state_ = ProcessState::Stopped;
  bool is_attached_ = true;
  std::unique_ptr<Registers> registers_;
  StoppointCollection<BreakpointSite> breakpoint_sites_;
  StoppointCollection<Watchpoint> watchpoints_;
  SyscallCatchPolicy syscall_catch_policy_ = SyscallCatchPolicy::CatchNone();

  // Whether we are waiting for a syscall exit.
  bool expecting_syscall_exit_ = false;
};

}  // namespace ldb