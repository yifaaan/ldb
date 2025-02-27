#include <sys/ptrace.h>
#include <sys/wait.h>
#include <unistd.h>

#include <libldb/error.hpp>
#include <libldb/process.hpp>


std::unique_ptr<ldb::Process> ldb::Process::Launch(std::filesystem::path path) {
  pid_t pid;
  if ((pid = fork()) < 0) {
    Error::SendErrno("fork failed");
  }
  if (pid == 0) {
    // `PTRACE_TRACEME`的作用
    // 子进程调用`PTRACE_TRACEME`后，内核会在其`task_struct`中设置`PT_PTRACED`标志，标记该进程为被跟踪状态。
    // 此时子进程不会主动停止，但后续的`execve()`系统调用会触发内核的调试拦截机制。
    if (ptrace(PTRACE_TRACEME, 0, nullptr, nullptr) < 0) {
      Error::SendErrno("Tracing failed");
    }
    // Execute the program.
    // Arguments list must be terminated by nullptr.
    // `execve()`的暂停行为
    // 当子进程调用`execve()`加载新程序时，内核会在新程序入口点（如`_start`）执行前插入一个调试陷阱，由内核自动发送`SIGTRAP`信号给子进程。
    // 子进程因`SIGTRAP`进入`TASK_STOPPED`状态，此时父进程需要通过`waitpid()`同步这一状态变更。
    if (execlp(path.c_str(), path.c_str(), nullptr) < 0) {
      Error::SendErrno("exec failed");
    }
  }
  std::unique_ptr<Process> process{new Process(pid, /*terminate_on_end=*/true)};
  process->WaitOnSignal();
  return process;
}

std::unique_ptr<ldb::Process> ldb::Process::Attach(pid_t pid) {
  if (pid == 0) {
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
      new Process(pid, /*terminate_on_end=*/false));
  process->WaitOnSignal();
  return process;
}

ldb::Process::~Process() {
  if (pid_ != 0) {
    int status;
    // If the process is running, stop it.
    if (state_ == ProcessState::kRunning) {
      kill(pid_, SIGSTOP);
      waitpid(pid_, &status, 0);
    }
    // Detach the process.解除调试关系
    ptrace(PTRACE_DETACH, pid_, nullptr, nullptr);
    // Resume the process.
    kill(pid_, SIGCONT);
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
  state_ = ProcessState::kRunning;
}