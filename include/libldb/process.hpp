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

class Process {
 public:
  Process() = delete;
  Process(const Process&) = delete;
  Process& operator=(const Process&) = delete;
  ~Process();

  static std::unique_ptr<Process> Launch(std::filesystem::path path);
  static std::unique_ptr<Process> Attach(pid_t pid);

  void Resume();

  void WaitOnSignal();

  pid_t Pid() const { return pid_; }

  ProcessState State() const { return state_;}

  private:
    Process(pid_t pid, bool terminate_on_end)
      : pid_{pid}, terminate_on_end_{terminate_on_end} {}

 private:
  pid_t pid_ = 0;
  bool terminate_on_end_ = true;
  ProcessState state_ = ProcessState::kStopped;
};
}  // namespace ldb