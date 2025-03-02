#include <sys/ptrace.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <unistd.h>

#include <libldb/error.hpp>
#include <libldb/pipe.hpp>
#include <libldb/process.hpp>

#include "libldb/breakpoint_site.hpp"

namespace {

// Failed to execute the program.
// Write the error message to the pipe.
// Exit the child process.
void ExitWithPerror(ldb::Pipe& channel, const std::string& prefix) {
  auto message = prefix + std::string(": ") + std::strerror(errno);
  channel.Write(reinterpret_cast<std::byte*>(message.data()), message.size());
  exit(-1);
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
  // If debug is true, wait for the child process to exit.
  // If debug is false, the child process will not be paused.
  if (debug) {
    process->WaitOnSignal();
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
  if (ptrace(PTRACE_CONT, pid_, nullptr, nullptr) < 0) {
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
  }
  return reason;
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

ldb::BreakpointSite& ldb::Process::CreateBreakpointSite(VirtAddr address) {
  if (breakpoint_sites_.ContainsAddress(address)) {
    Error::Send("Breakpoint site already created at address " +
                std::to_string((address.addr())));
  }
  return breakpoint_sites_.Push(
      std::unique_ptr<BreakpointSite>(new BreakpointSite{*this, address}));
}