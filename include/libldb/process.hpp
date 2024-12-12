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

class process {
public:
  static std::unique_ptr<process> launch(std::filesystem::path path);
  static std::unique_ptr<process> attach(pid_t pid);

  process() = delete;
  process(const process &) = delete;
  process &operator=(const process &) = delete;
  ~process();
  void resume();
  void wait_on_signal();

  pid_t pid() const { return pid_; }
  process_state state() const { return state_; }

private:
  /// For static member fn to construct a process
  process(pid_t pid, bool terminate_on_end)
      : pid_(pid), terminate_on_end_(terminate_on_end) {}

private:
  pid_t pid_ = 0;
  bool terminate_on_end_ = true;
  process_state state_ = process_state::stopped;
};

} // namespace ldb

#endif