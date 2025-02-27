#pragma once

#include <sys/types.h>

#include <filesystem>
#include <memory>
namespace ldb {
enum class ProcessState {
  kStopped,
  kRunning,
  kExited,
  kTerminated,
};

// The reason why the child process stopped.
struct StopReason {
  StopReason(int wait_status);

  ProcessState reason;
  std::uint8_t info;
};

class Process {
 public:
  Process() = delete;
  Process(const Process&) = delete;
  Process& operator=(const Process&) = delete;
  ~Process();

  // Launch a new process and return a pointer to it.
  // When the child pauses, the kernel will send a SIGCHLD signal to parent.
  static std::unique_ptr<Process> Launch(std::filesystem::path path);

  // Attach to a running process and return a pointer to it.
  // When the child pauses, the kernel will send a SIGCHLD signal to parent.
  static std::unique_ptr<Process> Attach(pid_t pid);

  // Resume the child process to continue execution.
  void Resume();

  // Wait the child process's change of state.
  // Return the reason why the child process stopped.
  StopReason WaitOnSignal();

  pid_t Pid() const { return pid_; }

  ProcessState State() const { return state_; }

 private:
  Process(pid_t pid, bool terminate_on_end)
      : pid_{pid}, terminate_on_end_{terminate_on_end} {}

 private:
  pid_t pid_ = 0;
  bool terminate_on_end_ = true;
  ProcessState state_ = ProcessState::kStopped;
};
}  // namespace ldb