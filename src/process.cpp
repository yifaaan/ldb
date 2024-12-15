#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <libldb/error.hpp>
#include <libldb/pipe.hpp>
#include <libldb/process.hpp>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
void exit_with_perror(ldb::pipe& channel, const std::string& prefix) {
  auto message = prefix + std::string(": ") + std::strerror(errno);
  channel.write(reinterpret_cast<std::byte*>(message.data()), message.size());
  exit(-1);
}
} // namespace
std::unique_ptr<ldb::process> ldb::process::launch(std::filesystem::path path,
                                                   bool debug) {
  // Child auto close channel when exec succeed,
  // because of close_on_exec.
  ldb::pipe channel(/*close_on_exec=*/true);
  pid_t pid;
  if ((pid = fork()) < 0) {
    error::send_errno("Fork failed");
  }

  if (pid == 0) {
    if (debug and ptrace(PTRACE_TRACEME, 0, nullptr, nullptr) < 0) {
      exit_with_perror(channel, "Tracing failed");
    }
    if (execlp(path.c_str(), path.c_str(), nullptr) < 0) {
      exit_with_perror(channel, "Exec failed");
    }
  }
  // Parent close the write side of the pipe.
  channel.close_write();
  auto data = channel.read();
  channel.close_read();

  if (data.size() > 0) {
    waitpid(pid, nullptr, 0);
    auto chars = reinterpret_cast<char*>(data.data());
    error::send(std::string(chars, chars + data.size()));
  }
  std::unique_ptr<process> proc(
      new process(pid, /*terminate_on_end=*/true, debug));
  if (debug) {
    proc->wait_on_signal();
  }
  return proc;
}

std::unique_ptr<ldb::process> ldb::process::attach(pid_t pid) {
  if (pid == 0) {
    error::send("Invalid PID");
  }
  if (ptrace(PTRACE_ATTACH, pid, nullptr, nullptr) < 0) {
    error::send_errno("Could not attach");
  }

  std::unique_ptr<process> proc(
      new process(pid, /*terminate_on_end=*/false, true));
  proc->wait_on_signal();

  return proc;
}

ldb::process::~process() {
  if (pid_ != 0) {
    int status;
    if (is_attached_) {
      if (state_ == process_state::running) {
        kill(pid_, SIGSTOP);
        waitpid(pid_, &status, 0);
      }
      ptrace(PTRACE_DETACH, pid_, nullptr, nullptr);
      kill(pid_, SIGCONT);
    }

    if (terminate_on_end_) {
      kill(pid_, SIGKILL);
      waitpid(pid_, &status, 0);
    }
  }
}

void ldb::process::resume() {
  // auto a = ptrace(PTRACE_CONT, pid_, nullptr, nullptr);
  // std::cout << std::format("ptrace ret: {}\n", a);
  if (ptrace(PTRACE_CONT, pid_, nullptr, nullptr) < 0) {
    error::send_errno("Could not resume");
  }

  state_ = process_state::running;
}

ldb::stop_reason ldb::process::wait_on_signal() {
  int wait_status;
  int options = 0;
  if (waitpid(pid_, &wait_status, options) < 0) {
    error::send_errno("waitpid failed");
  }
  stop_reason reason(wait_status);
  state_ = reason.reason;

  if (is_attached_ and state_ == process_state::stopped) {
    read_all_registers();
  }
  return reason;
}

ldb::stop_reason::stop_reason(int wait_status) {
  if (WIFEXITED(wait_status)) {
    reason = process_state::exited;
    info = WEXITSTATUS(wait_status);
  } else if (WIFSIGNALED(wait_status)) {
    reason = process_state::terminated;
    info = WTERMSIG(wait_status);
  } else if (WIFSTOPPED(wait_status)) {
    reason = process_state::stopped;
    info = WSTOPSIG(wait_status);
  }
}

void ldb::process::read_all_registers() {
  // Read the GPRs using PTRACE_GETREGS
  if (ptrace(PTRACE_GETREGS, pid_, nullptr, &get_registers().data_.regs) < 0) {
    error::send_errno("Could not read GPR registers");
  }
  if (ptrace(PTRACE_GETFPREGS, pid_, nullptr, &get_registers().data_.i387) <
      0) {
    error::send_errno("Could not read FPR registers");
  }
  for (int i = 0; i < 8; i++) {
    // Read debug registers
    auto id = static_cast<int>(register_id::dr0) + i;
    auto info = register_info_by_id(static_cast<register_id>(id));

    errno = 0;
    std::int64_t data = ptrace(PTRACE_PEEKUSER, pid_, info.offset, nullptr);
    if (errno != 0)
      error::send_errno("Could not read debug register");

    get_registers().data_.u_debugreg[i] = data;
  }
}

/// Call PTRACE_POKEUSER to write the given data to the user area at the given
/// offset.
void ldb::process::write_user_area(std::size_t offset, std::uint64_t data) {
  if (ptrace(PTRACE_POKEUSER, pid_, offset, data) < 0) {
    error::send_errno("Could not write to user area");
  }
}

void ldb::process::write_fprs(const user_fpregs_struct& fprs) {
  if (ptrace(PTRACE_SETFPREGS, pid_, nullptr, &fprs) < 0) {
    error::send_errno("Could not write floating point registers");
  }
}

void ldb::process::write_gprs(const user_regs_struct& gprs) {
  if (ptrace(PTRACE_SETREGS, pid_, nullptr, &gprs) < 0) {
    error::send_errno("Could not write general point registers");
  }
}
