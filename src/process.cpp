#include <bits/types/struct_iovec.h>
#include <fmt/ranges.h>
#include <spdlog/spdlog.h>
#include <sys/personality.h>
#include <sys/ptrace.h>
#include <sys/uio.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <unistd.h>

#include <csignal>
#include <iterator>
#include <libldb/error.hpp>
#include <libldb/pipe.hpp>
#include <libldb/process.hpp>
#include <utility>
#include <vector>

#include "libldb/bit.hpp"
#include "libldb/breakpoint_site.hpp"
#include "libldb/register_info.hpp"
#include "libldb/types.hpp"
#include "libldb/watchpoint.hpp"

namespace {

// Failed to execute the program.
// Write the error message to the pipe.
// Exit the child process.
void ExitWithPerror(ldb::Pipe& channel, const std::string& prefix) {
  auto message = prefix + std::string(": ") + std::strerror(errno);
  channel.Write(reinterpret_cast<std::byte*>(message.data()), message.size());
  exit(-1);
}

// Encode the hardware stoppoint mode.
// 00 - 执行断点
// 01 - 数据写入断点
// 10 - I/O读写（通常不支持）
// 11 - 数据读写断点
std::uint64_t EncodeHardwareStoppointMode(ldb::StoppointMode mode) {
  switch (mode) {
    case ldb::StoppointMode::Write:
      return 0b01;
    case ldb::StoppointMode::ReadWrite:
      return 0b11;
    case ldb::StoppointMode::Execute:
      return 0b00;
    default:
      ldb::Error::Send("Invalid stoppoint size");
  }
}

// 1 byte (00b): The breakpoint is triggered if the CPU accesses a single byte
// at the specified address.

// 2 bytes (01b): The breakpoint is triggered if the CPU accesses a 2-byte
// (word) value at the specified address.

// 4 bytes (11b): The breakpoint is triggered if the CPU accesses a 4-byte
// (dword) value at the specified address.

// 8 bytes (10b): This setting is often undefined or reserved in many
// architectures. However, in some contexts, it might be used for larger data
// types or specific debugging scenarios.
std::uint64_t EncodeHardwareStoppointSize(std::size_t size) {
  switch (size) {
    case 1:
      return 0b00;
    case 2:
      return 0b01;
    case 4:
      return 0b11;
    case 8:
      return 0b10;
    default:
      ldb::Error::Send("Invalid stoppoint size");
  }
}

// Find a free hardware debug register.
int FindFreeStoppointRegister(std::uint64_t control_reg) {
  for (int i = 0; i < 4; i++) {
    if ((control_reg & (0b11 << (i * 2))) == 0) {
      return i;
    }
  }
  ldb::Error::Send("No remaining hardware debug registers");
}

void SetPtraceOptions(pid_t pid) {
  if (ptrace(PTRACE_SETOPTIONS, pid, nullptr, PTRACE_O_TRACESYSGOOD) < 0) {
    ldb::Error::SendErrno("Failed to set TRACESYSGOOD option");
  }
}

}  // namespace

ldb::StopReason::StopReason(int wait_status) {
  if (WIFEXITED(wait_status)) {
    // Normal exit.
    reason = ProcessState::Exited;
    // Get the exit status.
    info = WEXITSTATUS(wait_status);
  } else if (WIFSIGNALED(wait_status)) {
    // Terminated by a signal.
    reason = ProcessState::Terminated;
    // Get the signal number.
    info = WTERMSIG(wait_status);
  } else if (WIFSTOPPED(wait_status)) {
    // Stopped by a signal.
    reason = ProcessState::Stopped;
    // Get the signal number.
    info = WSTOPSIG(wait_status);
  }
}

std::unique_ptr<ldb::Process> ldb::Process::Launch(
    std::filesystem::path path, bool debug,
    std::optional<int> stdout_replacement) {
  // 创建一个管道，用于父子进程之间的通信
  Pipe channel(/*close_on_exec=*/true);
  pid_t pid;
  if ((pid = fork()) < 0) {
    Error::SendErrno("fork failed");
  }
  if (pid == 0) {
    // Set the inferior's process group id to itself.
    // This is to avoid the inferior process being received the signal from
    // the parent process.
    if (setpgid(0, 0) < 0) {
      ExitWithPerror(channel, "Could not set pgid");
    }
    // Close the randomization of the address space.
    // The load virtual address of the program is fixed.
    personality(ADDR_NO_RANDOMIZE);
    channel.CloseRead();
    if (stdout_replacement) {
      // If stdout_replacement is provided, replace the child process's stdout
      // with the file descriptor. This means the child process's stdout will
      // be redirected to the file descriptor.
      if (dup2(*stdout_replacement, STDOUT_FILENO) < 0) {
        ExitWithPerror(channel, "stdout replacement failed");
      }
    }
    // `PTRACE_TRACEME`的作用
    // 子进程调用`PTRACE_TRACEME`后，内核会在其`task_struct`中设置`PT_PTRACED`标志，标记该进程为被跟踪状态。
    // 此时子进程不会主动停止，但后续的`execve()`系统调用会触发内核的调试拦截机制。
    if (debug && ptrace(PTRACE_TRACEME, 0, nullptr, nullptr) < 0) {
      ExitWithPerror(channel, "Tracing failed");
    }
    // Execute the program.
    // Arguments list must be terminated by nullptr.
    // `execve()`的暂停行为
    // 当子进程调用`execve()`加载新程序时，内核会在新程序入口点（如`_start`）执行前插入一个调试陷阱，由内核自动发送`SIGTRAP`信号给子进程。
    // 子进程因`SIGTRAP`进入`TASK_STOPPED`状态，此时父进程需要通过`waitpid()`同步这一状态变更。
    if (execlp(path.c_str(), path.c_str(), nullptr) < 0) {
      ExitWithPerror(channel, "exec failed");
    }
  }
  channel.CloseWrite();
  // Once the child process exec is successful, it will close the write end of
  // the pipe. The the Read() will return EOF.
  auto data = channel.Read();

  // If the data is not empty, it means the child process has exited.
  // We need to wait for the child process to exit and get the exit status.
  if (data.size() > 0) {
    waitpid(pid, nullptr, 0);
    auto chars = reinterpret_cast<char*>(data.data());
    // Throw the error message in parent process.
    Error::Send(std::string(chars, chars + data.size()));
  }
  std::unique_ptr<Process> process{
      new Process(pid, /*terminate_on_end=*/true, debug)};
  // waitpid processes SIGTRAP for execlp
  if (debug) {
    process->WaitOnSignal();
    SetPtraceOptions(pid);
  }
  return process;
}

std::unique_ptr<ldb::Process> ldb::Process::Attach(pid_t pid) {
  if (pid <= 0) {
    Error::Send("Invalid pid");
  }
  // `PTRACE_ATTACH`的作用
  // 父进程使用`PTRACE_ATTACH`跟踪子进程，内核会在子进程的`task_struct`中设置`PT_PTRACED`标志，标记该进程为被跟踪状态。
  // 子进程会收到内核发送的`SIGSTOP`信号，并进入`TASK_STOPPED`状态。
  // 父进程需要通过`waitpid()`同步这一状态变更。
  if (ptrace(PTRACE_ATTACH, pid, nullptr, nullptr) < 0) {
    Error::SendErrno("Could not attach");
  }
  // Attached process will not be terminated on end.
  std::unique_ptr<Process> process(
      new Process(pid, /*terminate_on_end=*/false, /*is_attached=*/true));
  process->WaitOnSignal();
  SetPtraceOptions(process->pid());
  return process;
}

ldb::Process::~Process() {
  if (pid_ != 0) {
    int status;
    if (is_attached_) {
      // If the process is running, stop it.
      if (state_ == ProcessState::Running) {
        kill(pid_, SIGSTOP);
        waitpid(pid_, &status, 0);
      }
      // Detach the process.解除调试关系
      ptrace(PTRACE_DETACH, pid_, nullptr, nullptr);
      // Resume the process.
      kill(pid_, SIGCONT);
    }

    // If terminate on end, kill it.
    if (terminate_on_end_) {
      kill(pid_, SIGKILL);
      waitpid(pid_, &status, 0);
    }
  }
}

void ldb::Process::Resume() {
  // If the process is stopped at a breakpoint, we need to disable the
  // breakpoint and single step to the instruction begin address to continue
  // execution.
  auto pc = GetPc();
  if (breakpoint_sites_.EnabledStoppointAtAddress(pc)) {
    auto& bp = breakpoint_sites_.GetByAddress(pc);
    // Disable the breakpoint to continue execution.
    bp.Disable();
    if (ptrace(PTRACE_SINGLESTEP, pid_, nullptr, nullptr) < 0) {
      Error::SendErrno("Failed to single step");
    }
    int wait_status;
    if (waitpid(pid_, &wait_status, 0) < 0) {
      Error::SendErrno("waitpid failed");
    }
    // Enable the breakpoint again.
    bp.Enable();
  }

  // Whether to catch syscalls.
  // When catch syscall, the process will trap on the syscall entry and exit.
  auto request = syscall_catch_policy_.mode() == SyscallCatchPolicy::Mode::None
                     ? PTRACE_CONT
                     : PTRACE_SYSCALL;
  if (ptrace(request, pid_, nullptr, nullptr) < 0) {
    Error::SendErrno("Could not resume");
  }
  state_ = ProcessState::Running;
}

ldb::StopReason ldb::Process::WaitOnSignal() {
  int wait_status;
  int options = 0;
  if (waitpid(pid_, &wait_status, options) < 0) {
    Error::SendErrno("waitpid failed");
  }
  StopReason reason{wait_status};
  // Update the process state.
  state_ = reason.reason;

  // Only the process is attached and stopped, read all the registers.
  if (is_attached_ && state_ == ProcessState::Stopped) {
    ReadAllRegisters();
    AugmentStopReason(reason);

    // When the process is stopped by int3 instruction, set the program counter
    // to the instruction begin address to continue execution.
    auto instruction_begin = GetPc() - 1;
    if (reason.info == SIGTRAP) {
      if (reason.trap_reason == TrapType::SoftwareBreak &&
          breakpoint_sites_.ContainsAddress(instruction_begin) &&
          breakpoint_sites_.GetByAddress(instruction_begin).IsEnabled()) {
        SetPc(instruction_begin);
      } else if (reason.trap_reason == TrapType::HardwareBreak) {
        auto id = GetCurrentHardwareStoppoint();
        if (id.index() == 1) {
          watchpoints_.GetById(std::get<1>(id)).UpdateData();
        }
      } else if (reason.trap_reason == TrapType::Syscall) {
        reason = MaybeResumeFromSyscall(reason);
      }
    }
  }
  return reason;
}

void ldb::Process::AugmentStopReason(StopReason& reason) {
  // Get the signal information.
  siginfo_t info;
  if (ptrace(PTRACE_GETSIGINFO, pid_, nullptr, &info) < 0) {
    Error::SendErrno("Failed to get signal info");
  }

  // If the process stopped because of a syscall, fill the syscall information.
  if (reason.info == (SIGTRAP | 0x80)) {
    auto& sys_info = reason.syscall_info.emplace();
    auto& regs = registers();
    reason.info = SIGTRAP;
    reason.trap_reason = TrapType::Syscall;
    // if (auto found =
    //         std::ranges::find(syscall_catch_policy_.to_catch(), sys_info.id);
    //     found != std::end(syscall_catch_policy_.to_catch())) {

    // }
    // If we are expecting a syscall exit, fill the return value.
    if (expecting_syscall_exit_) {
      sys_info.entry = false;
      sys_info.id = regs.ReadByIdAs<std::uint64_t>(RegisterId::orig_rax);
      if (sys_info.id == 1) {
        sys_info.ret = regs.ReadByIdAs<std::uint64_t>(RegisterId::rax);
      }
      expecting_syscall_exit_ = false;
    } else {
      sys_info.entry = true;
      sys_info.id = regs.ReadByIdAs<std::uint64_t>(RegisterId::orig_rax);
      std::array<RegisterId, 6> arg_regs = {RegisterId::rdi, RegisterId::rsi,
                                            RegisterId::rdx, RegisterId::r10,
                                            RegisterId::r8,  RegisterId::r9};
      for (int i = 0; i < 6; i++) {
        sys_info.args[i] = regs.ReadByIdAs<std::uint64_t>(arg_regs[i]);
      }
      expecting_syscall_exit_ = true;
    }
    return;
  }

  // We didn't stop due to a syscall. We need to reset the flag.
  expecting_syscall_exit_ = false;

  reason.trap_reason = TrapType::Unknown;
  if (reason.info == SIGTRAP) {
    switch (info.si_code) {
      case TRAP_TRACE:
        reason.trap_reason = TrapType::SingleStep;
        break;
      case SI_KERNEL:
        reason.trap_reason = TrapType::SoftwareBreak;
        break;
      case TRAP_HWBKPT:
        reason.trap_reason = TrapType::HardwareBreak;
        break;
    }
  }
}

void ldb::Process::WriteFprs(const user_fpregs_struct& fprs) {
  if (ptrace(PTRACE_SETFPREGS, pid_, nullptr, &fprs) < 0) {
    Error::SendErrno("Could not write floating point registers");
  }
}

void ldb::Process::WriteGprs(const user_regs_struct& gprs) {
  if (ptrace(PTRACE_SETREGS, pid_, nullptr, &gprs) < 0) {
    Error::SendErrno("Could not write floating point registers");
  }
}

void ldb::Process::WriteUserArea(std::size_t offset, std::uint64_t data) {
  // The offset must be aligned to 8 bytes.
  // Otherwise, ptrace will return EIO error.
  if (ptrace(PTRACE_POKEUSER, pid_, offset, data) < 0) {
    Error::SendErrno("Could not write to user area");
  }
}

void ldb::Process::ReadAllRegisters() {
  if (ptrace(PTRACE_GETREGS, pid_, nullptr, &registers().data_.regs) < 0) {
    Error::SendErrno("Could not read GPR registers");
  }
  if (ptrace(PTRACE_GETFPREGS, pid_, nullptr, &registers().data_.i387) < 0) {
    Error::SendErrno("Could not read FPR registers");
  }
  // Return debug registers.
  for (int i = 0; i < 8; i++) {
    auto id = static_cast<int>(RegisterId::dr0) + i;
    auto info = RegisterInfoById(static_cast<RegisterId>(id));

    errno = 0;
    // Get the value of the debug register.
    std::int64_t data = ptrace(PTRACE_PEEKUSER, pid_, info.offset, nullptr);
    if (errno != 0) {
      Error::SendErrno("Could not read debug register");
    }
    // Write the value to the register object.
    registers().data_.u_debugreg[i] = data;
  }
}

ldb::BreakpointSite& ldb::Process::CreateBreakpointSite(VirtAddr address,
                                                        bool hardware,
                                                        bool internal) {
  if (breakpoint_sites_.ContainsAddress(address)) {
    Error::Send("Breakpoint site already created at address " +
                std::to_string(address.addr()));
  }
  return breakpoint_sites_.Push(std::unique_ptr<BreakpointSite>(
      new BreakpointSite{*this, address, hardware, internal}));
}

ldb::Watchpoint& ldb::Process::CreateWatchpoint(VirtAddr address,
                                                StoppointMode mode,
                                                std::size_t size) {
  if (watchpoints_.ContainsAddress(address)) {
    Error::Send("Watchpoint already created at address " +
                std::to_string(address.addr()));
  }
  return watchpoints_.Push(
      std::unique_ptr<Watchpoint>(new Watchpoint{*this, address, mode, size}));
}

ldb::StopReason ldb::Process::StepInstruction() {
  std::optional<BreakpointSite*> to_reenable;
  // If a breakpoint is enabled at the current program counter, disable it
  // and remember to reenable it after single stepping.
  auto pc = GetPc();
  if (breakpoint_sites_.EnabledStoppointAtAddress(pc)) {
    auto& bp = breakpoint_sites_.GetByAddress(pc);
    bp.Disable();
    to_reenable = &bp;
  }
  if (ptrace(PTRACE_SINGLESTEP, pid_, nullptr, nullptr) < 0) {
    Error::SendErrno("Failed to single step");
  }
  auto reason = WaitOnSignal();
  if (to_reenable) {
    to_reenable.value()->Enable();
  }
  return reason;
}

std::vector<std::byte> ldb::Process::ReadMemory(VirtAddr address,
                                                std::size_t size) const {
  std::vector<std::byte> buf(size);

  iovec local_iov{buf.data(), buf.size()};

  std::vector<iovec> remote_descs;

  while (size > 0) {
    auto up_to_next_page = 0x1000 - address.addr() & 0xfff;
    auto chunk_size = std::min(size, up_to_next_page);

    remote_descs.emplace_back(reinterpret_cast<void*>(address.addr()),
                              chunk_size);
    size -= chunk_size;
    address += chunk_size;
  }

  if (process_vm_readv(pid_, &local_iov, 1, remote_descs.data(),
                       remote_descs.size(), 0) < 0) {
    Error::SendErrno("Could not read process memory");
  }
  return buf;
}

std::vector<std::byte> ldb::Process::ReadMemoryWithoutTraps(
    VirtAddr address, std::size_t size) const {
  auto memory = ReadMemory(address, size);

  // Get all breakpoint sites in the region.
  auto sites = breakpoint_sites_.GetInRegion(address, address + size);

  for (auto site : sites) {
    // 如果断点未启用或为硬件断点，则跳过
    if (!site->IsEnabled() || site->IsHardware()) {
      continue;
    }
    auto offset = site->address() - address.addr();
    memory[offset.addr()] = site->saved_data_;
  }
  return memory;
}

void ldb::Process::WriteMemory(VirtAddr address,
                               std::span<const std::byte> data) {
  std::size_t written = 0;
  while (written < data.size()) {
    auto remaining = data.size() - written;
    std::uint64_t word;
    if (remaining >= sizeof(word)) {
      word = FromBytes<std::uint64_t>(data.data() + written);
    } else {
      // Read the data from source
      // remaining = 3
      // 0xffffff **********
      auto read = ReadMemory(address + written, sizeof(word));
      auto word_data = reinterpret_cast<char*>(&word);
      // Copy the remaining to the word: 0xffffff
      std::memcpy(word_data, data.data() + written, remaining);
      // Copy the source data: 0x**********
      std::memcpy(word_data + remaining, read.data() + remaining,
                  sizeof(word) - remaining);
    }
    if (ptrace(PTRACE_POKEDATA, pid_, address + written, word) < 0) {
      Error::SendErrno("Failed to write memory");
    }
    written += sizeof(word);
  }
}

int ldb::Process::SetHardwareBreakpoint(ldb::BreakpointSite::IdType id,
                                        VirtAddr address) {
  return SetHardwareStoppoint(address, StoppointMode::Execute, 1);
}

int ldb::Process::SetHardwareStoppoint(VirtAddr address, StoppointMode mode,
                                       std::size_t size) {
  // Read the control register.
  auto& regs = registers();
  auto control_reg = regs.ReadByIdAs<std::uint64_t>(RegisterId::dr7);
  int free_space = FindFreeStoppointRegister(control_reg);

  // Get the free register id.
  auto id = static_cast<int>(RegisterId::dr0) + free_space;
  // Write the hardware breakpoint address to the register.
  regs.WriteById(static_cast<RegisterId>(id), address.addr());

  auto mode_flag = EncodeHardwareStoppointMode(mode);
  auto size_flag = EncodeHardwareStoppointSize(size);

  // The DR7 register, also known as the Debug Control Register,
  //     plays a crucial role in controlling and configuring hardware
  //     breakpoints
  //         on x64 architectures.Here's a breakdown of its structure:

  // Bits	Description
  // 0-7
  // Breakpoint Enables
  // 0	Local enable for breakpoint #0 (L0)
  // 1	Global enable for breakpoint #0 (G0)
  // 2	Local enable for breakpoint #1 (L1)
  // 3	Global enable for breakpoint #1 (G1)
  // 4	Local enable for breakpoint #2 (L2)
  // 5	Global enable for breakpoint #2 (G2)
  // 6	Local enable for breakpoint #3 (L3)
  // 7	Global enable for breakpoint #3 (G3)
  // 8-9
  // Reserved (386 only: Local Exact Breakpoint Enable (LE) and Global Exact
  // Breakpoint Enable (GE))
  // 10
  // Reserved, read-only, read as 1 and should be written as 1
  // 11
  // RTM (Processors with Intel TSX only: Enable advanced
  // debugging of RTM transactions)
  // 12
  // IR, SMIE (386/486 only: Action on breakpoint match; otherwise reserved)
  // 13
  // GD (General Detect Enable:
  // causes a debug exception on any attempt to access DR0-DR7)
  // 14-15
  // Reserved, should be written as all-0s
  // 16-17
  // R/W0 (Breakpoint condition for breakpoint #0: 00b = execution break,
  // 01b = write watchpoint, 11b = R/W watchpoint)
  // 18-19
  // LEN0 (Breakpoint length for breakpoint #0: 00 = 1 byte, 01 = 2 bytes,
  // 10 = undefined or 8 bytes, 11 = 4 bytes)
  // 20-21
  // R/W1 (Breakpoint condition for breakpoint #1)
  // 22-23
  // LEN1 (Breakpoint length for breakpoint #1)
  // 24-25
  // R/W2 (Breakpoint condition for breakpoint #2)
  // 26-27
  // LEN2 (Breakpoint length for breakpoint #2)
  // 28-29
  // R/W3 (Breakpoint condition for breakpoint #3)
  // 30-31
  // LEN3 (Breakpoint length for breakpoint #3)
  // 32-35
  // Reserved for PTTT (Processor Trace Trigger Tracing) on some processors;
  // otherwise read as 0 and must be written as 0
  // 36-63
  // Reserved (x86-64 only), read as all-0s and must be written as all-0s
  auto enable_bit = 1 << (free_space * 2);
  auto mode_bits = mode_flag << (free_space * 4 + 16);
  auto size_bits = size_flag << (free_space * 4 + 18);
  auto clear_mask =
      (0b11 << (free_space * 2)) | (0b1111 << (free_space * 4 + 16));
  // Clear the enable, mode and size bits of the control register.
  auto masked = control_reg & ~clear_mask;
  // Set the enable, mode and size bits of the control register.
  masked |= enable_bit | mode_bits | size_bits;
  regs.WriteById(RegisterId::dr7, masked);
  return free_space;
}

int ldb::Process::SetWatchpoint(Watchpoint::IdType id, VirtAddr address,
                                StoppointMode mode, std::size_t size) {
  return SetHardwareStoppoint(address, mode, size);
}

void ldb::Process::ClearHardwareStoppoint(int index) {
  auto id = static_cast<int>(RegisterId::dr0) + index;
  // Clear the breakpoint address.
  registers().WriteById(static_cast<RegisterId>(id), 0);
  auto control_reg = registers().ReadByIdAs<std::uint64_t>(RegisterId::dr7);
  auto clear_mask = (0b11 << (index * 2)) | (0b1111 << (index * 4 + 16));
  // Clear the enable, mode and size bits of the control register.
  auto masked = control_reg & ~clear_mask;
  // Write the control register back.
  registers().WriteById(RegisterId::dr7, masked);
}

std::variant<ldb::BreakpointSite::IdType, ldb::Watchpoint::IdType>
ldb::Process::GetCurrentHardwareStoppoint() const {
  auto& regs = registers();
  auto status = regs.ReadByIdAs<std::uint64_t>(RegisterId::dr6);
  auto index = __builtin_ctzll(status);

  auto id = static_cast<int>(RegisterId::dr0) + index;
  // Get the address of the breakpoint.
  auto addr =
      VirtAddr{regs.ReadByIdAs<std::uint64_t>(static_cast<RegisterId>(id))};
  using Ret = std::variant<BreakpointSite::IdType, Watchpoint::IdType>;
  // If the address is a breakpoint address, return the breakpoint site id.
  if (breakpoint_sites_.ContainsAddress(addr)) {
    auto site_id = breakpoint_sites_.GetByAddress(addr).id();
    return Ret{std::in_place_index<0>, site_id};
  } else {
    auto watch_id = watchpoints_.GetByAddress(addr).id();
    return Ret{std::in_place_index<1>, watch_id};
  }
}

ldb::StopReason ldb::Process::MaybeResumeFromSyscall(const StopReason& reason) {
  if (syscall_catch_policy_.mode() == SyscallCatchPolicy::Mode::Some) {
    auto& to_catch = syscall_catch_policy_.to_catch();

    // If the syscall is not in the list of syscalls to catch, resume the
    // process and return the new stop reason.
    if (auto found = std::ranges::find(to_catch, reason.syscall_info->id);
        found == std::end(to_catch)) {
      Resume();
      return WaitOnSignal();
    }
  }
  return reason;
}