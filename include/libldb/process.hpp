#ifndef LDB_PROCESS_HPP
#define LDB_PROCESS_HPP

#include <filesystem>
#include <memory>
#include <sys/types.h>

namespace ldb {
enum class process_state {
  stopped,
  running,
  exited,
  terminated,
};

struct stop_reason {
  stop_reason(int wait_status);

  process_state reason;
  std::uint8_t info;
};

class process {
public:
  static std::unique_ptr<process> launch(std::filesystem::path path,
                                         bool debug = true);
  static std::unique_ptr<process> attach(pid_t pid);

  process() = delete;
  process(const process&) = delete;
  process& operator=(const process&) = delete;
  ~process();
  void resume();
  stop_reason wait_on_signal();

  pid_t pid() const { return pid_; }
  process_state state() const { return state_; }

private:
  /// For static member fn to construct a process
  process(pid_t pid, bool terminate_on_end, bool is_attached)
      : pid_(pid), terminate_on_end_(terminate_on_end),
        is_attached_(is_attached) {}

private:
  pid_t pid_ = 0;
  bool terminate_on_end_ = true;
  bool is_attached_ = true;
  process_state state_ = process_state::stopped;
};

} // namespace ldb

#endif