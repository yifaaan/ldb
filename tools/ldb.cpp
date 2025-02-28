#include <fmt/core.h>
#include <readline/history.h>
#include <readline/readline.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <iostream>
#include <libldb/error.hpp>
#include <libldb/libldb.hpp>
#include <libldb/process.hpp>
#include <sstream>
#include <string>
#include <vector>

namespace {
// attach to a process or a program
std::unique_ptr<ldb::Process> attach(int argc, const char** argv) {
  // Passing PID
  if (argc == 3 && argv[1] == std::string_view("-p")) {
    pid_t pid = std::atoi(argv[2]);
    return ldb::Process::Attach(pid);
  } else {
    // Passing program name.
    // We need to fork a child process to execute the program.
    const char* program_path = argv[1];
    return ldb::Process::Launch(program_path);
  }
}

// split a string by a delimiter
std::vector<std::string> split(std::string_view str, char delimiter) {
  std::vector<std::string> ret;
  std::string s;
  std::stringstream ss{std::string{str}};

  while (std::getline(ss, s, delimiter)) {
    if (s.empty()) continue;
    ret.push_back(s);
  }
  return ret;
}

// check if `str` is a prefix of `of`
bool is_prefix(std::string_view str, std::string_view of) {
  return of.starts_with(str);
}

void resume(pid_t pid) {
  if (ptrace(PTRACE_CONT, pid, /*addr*/ nullptr, /*data*/ nullptr) < 0) {
    std::perror("Could not continue");
    exit(1);
  }
}

void wait_on_signal(pid_t pid) {
  int wait_status;
  int options = 0;
  if (waitpid(pid, &wait_status, options) < 0) {
    std::perror("wailpid failed");
  }
  exit(1);
}

void print_stop_reason(const ldb::Process& process, ldb::StopReason reason) {
  fmt::print("Process {} ", process.pid());

  switch (reason.reason) {
    case ldb::ProcessState::Exited:
      fmt::println("exited with status {}", static_cast<int>(reason.info));
      break;
    case ldb::ProcessState::Terminated:
      fmt::println("terminated with signal {}", sigabbrev_np(reason.info));
      break;
    case ldb::ProcessState::Stopped:
      fmt::println("stopped with signal {}", sigabbrev_np(reason.info));
      break;
      // default:
      //   fmt::println("unknown stop reason");
      //   break;
  }
}

// handle command
void handle_command(std::unique_ptr<ldb::Process>& process,
                    std::string_view line) {
  auto args = split(line, ' ');
  auto command = args[0];

  if (is_prefix(command, "continue")) {
    process->Resume();
    auto reason = process->WaitOnSignal();
    print_stop_reason(*process, reason);
  } else {
    fmt::println("Unknown command: {}", command);
  }
}

void main_loop(std::unique_ptr<ldb::Process>& process) {
  // For now, the child process is paused.
  // We can accept commands from the user.
  char* line = nullptr;
  while ((line = readline("> ")) != nullptr) {
    std::string line_str;

    // If the line is empty, use the last command.
    if (line == std::string_view("")) {
      free(line);
      if (history_length > 0) {
        line_str = history_list()[history_length - 1]->line;
      }
    } else {
      line_str = line;
      add_history(line);
      free(line);
    }

    // Handle the command.
    if (!line_str.empty()) {
      try {
        handle_command(process, line_str);
      } catch (const ldb::Error& err) {
        fmt::println("{}", err.what());
      }
    }
  }
}
}  // namespace

int main(int argc, const char** argv) {
  if (argc == 1) {
    std::cerr << "No arguments given\n";
    return -1;
  }
  try {
    auto process = attach(argc, argv);
    main_loop(process);
  } catch (const ldb::Error& err) {
    fmt::println("{}", err.what());
  }
}