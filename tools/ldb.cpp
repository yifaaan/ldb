#include "libldb/error.hpp"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <libldb/process.hpp>
#include <readline/history.h>
#include <readline/readline.h>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace {

/// Launches and attaches to the given program name or PID.
/// Returns the PID of the inferior.
std::unique_ptr<ldb::process> attach(int argc, const char **argv) {
  pid_t pid = 0;
  // Passing PID.
  if (argc == 3 && argv[1] == std::string_view("-p")) {
    pid = std::atoi(argv[2]);
    return ldb::process::attach(pid);
  }
  // Passing program name.
  else {
    const char *program_path = argv[1];
    return ldb::process::launch(program_path);
  }
}
std::vector<std::string> split(std::string_view str, char delimiter) {
  std::vector<std::string> out{};
  std::stringstream ss{std::string{str}};
  std::string item;

  while (std::getline(ss, item, delimiter)) {
    out.push_back(item);
  }
  return out;
}

bool is_prefix(std::string_view str, std::string_view of) {
  if (str.size() > of.size())
    return false;
  return std::equal(str.begin(), str.end(), of.begin());
}

/// Resume the execution of the process.
void resume(pid_t pid) {
  if (ptrace(PTRACE_CONT, pid, nullptr, nullptr) < 0) {
    std::cerr << "Couldn't continue\n";
    std::exit(-1);
  }
}
void wait_on_signal(pid_t pid) {
  int wait_status;
  int options = 0;
  if (waitpid(pid, &wait_status, options) < 0) {
    std::perror("Waitpid failed");
    std::exit(-1);
  }
}

void print_stop_reason(const ldb::process &process, ldb::stop_reason reason) {
  // Print out the inferior's PID.
  std::cout << std::format("Process {} ", process.pid());

  switch (reason.reason) {
  case ldb::process_state::exited:
    std::cout << std::format("exited with status {}",
                             static_cast<int>(reason.info));
    break;
  case ldb::process_state::terminated:
    std::cout << std::format("terminated with signal {}",
                             sigabbrev_np(reason.info));
    break;
  case ldb::process_state::stopped:
    std::cout << std::format("stopped with signal {}",
                             sigabbrev_np(reason.info));
    break;
  }
  std::cout << std::endl;
}

void handle_command(std::unique_ptr<ldb::process> &process,
                    std::string_view line) {
  auto args = split(line, ' ');
  auto command = args[0];

  if (is_prefix(command, "continue")) {
    process->resume();
    auto reason = process->wait_on_signal();
    print_stop_reason(*process, reason);
  } else {
    std::cerr << "Unknown command\n";
  }
}

void main_loop(std::unique_ptr<ldb::process> &process) {
  char *line = nullptr;
  // Reading input from user.
  while ((line = readline("ldb> ")) != nullptr) {
    std::string line_str;
    // If is an empty line.
    // Re-run the last command.
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
    if (!line_str.empty()) {
      try {
        handle_command(process, line_str);
      } catch (const ldb::error &err) {
        std::cout << err.what() << '\n';
      }
    }
  }
}
} // namespace

int main(int argc, const char **argv) {
  if (argc == 1) {
    std::cerr << "No arguments given\n";
    return -1;
  }
  try {
    auto process = attach(argc, argv);
    main_loop(process);
  } catch (const ldb::error &err) {
    std::cout << err.what() << '\n';
  }
}