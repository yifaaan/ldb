#include <csignal>
#include <libldb/error.hpp>
#include <libldb/process.hpp>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

std::unique_ptr<ldb::process> ldb::process::launch(std::filesystem::path path) {
  pid_t pid;
  if ((pid = fork()) < 0) {
    // Error: fork failed
  }

  if (pid == 0) {
    if (ptrace(PTRACE_TRACEME, 0, nullptr, nullptr) < 0) {
      error::send_errno("Tracing failed");
    }
    if (execlp(path.c_str(), path.c_str(), nullptr) < 0) {
      error::send_errno("exec failed");
    }
  }

  std::unique_ptr<process> proc(new process(pid, /*terminate_on_end=*/true));
  proc->wait_on_signal();
  return proc;
}

std::unique_ptr<ldb::process> ldb::process::attach(pid_t pid) {
  if (pid == 0) {
    error::send("Invalid PID");
  }
  if (ptrace(PTRACE_ATTACH, pid, nullptr, nullptr) < 0) {
    error::send_errno("Could not attach");
  }

  std::unique_ptr<process> proc(new process(pid, /*terminate_on_end=*/false));
  proc->wait_on_signal();

  return proc;
}

ldb::process::~process() {
  if (pid_ != 0) {
    int status;
    if (state_ == process_state::running) {
      kill(pid_, SIGSTOP);
      waitpid(pid_, &status, 0);
    }
    ptrace(PTRACE_DETACH, pid_, nullptr, nullptr);
    kill(pid_, SIGCONT);

    if (terminate_on_end_) {
      kill(pid_, SIGKILL);
      waitpid(pid_, &status, 0);
    }
  }
}