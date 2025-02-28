#pragma once

#include <sys/types.h>

#include <filesystem>
#include <libldb/registers.hpp>
#include <memory>

namespace ldb {
enum class ProcessState {
  Stopped,
  Running,
  Exited,
  Terminated,
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
  static std::unique_ptr<Process> Launch(std::filesystem::path path,
                                         bool debug = true);

  // Attach to a running process and return a pointer to it.
  // When the child pauses, the kernel will send a SIGCHLD signal to parent.
  static std::unique_ptr<Process> Attach(pid_t pid);

  // Resume the child process to continue execution.
  void Resume();

  // Wait the child process's change of state.
  // Return the reason why the child process stopped.
  StopReason WaitOnSignal();

  pid_t pid() const { return pid_; }

  ProcessState state() const { return state_; }

  // Get the registers of the process.
  Registers& registers() { return *registers_; }
  // Get the registers of the process.
  const Registers& registers() const { return *registers_; }

  // ptrace provides an area of memory in the same format as the user struct,
  // called the user area, which we can write into to update a single register
  // value.
  void WriteUserArea(std::size_t offset, std::uint64_t value);

 private:
  Process(pid_t pid, bool terminate_on_end, bool is_attached)
      : pid_{pid},
        terminate_on_end_{terminate_on_end},
        is_attached_{is_attached},
        registers_{new Registers{*this}} {}

  // Read all the registers of the process to the registers object.
  void ReadAllRegisters();

 private:
  pid_t pid_ = 0;
  bool terminate_on_end_ = true;
  ProcessState state_ = ProcessState::Stopped;
  bool is_attached_ = true;
  std::unique_ptr<Registers> registers_;
};
}  // namespace ldb