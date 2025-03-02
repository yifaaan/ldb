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
#include <span>
#include <sstream>
#include <string>
#include <vector>

namespace {
// attach to a process or a program
std::unique_ptr<ldb::Process> Attach(int argc, const char** argv) {
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
std::vector<std::string> Split(std::string_view str, char delimiter) {
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
bool IsPrefix(std::string_view str, std::string_view of) {
  return of.starts_with(str);
}

void PrintStopReason(const ldb::Process& process, ldb::StopReason reason) {
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

// help ...
void PrintHelp(std::span<const std::string> args) {
  if (args.size() == 1) {
    fmt::println(stderr, R"(Available commands:
    continue    - Resume the process
    register    - Commands for operating on registers)");
  } else if (IsPrefix(args[1], "register")) {
    fmt::println(stderr, R"(Available commands:
    read
    read <register>
    read all
    write <register> <value>)");
  } else {
    fmt::println(stderr, "No help available on that");
  }
}

// handle command
void HandleCommand(std::unique_ptr<ldb::Process>& process,
                   std::string_view line) {
  auto args = Split(line, ' ');
  auto command = args[0];

  if (IsPrefix(command, "continue")) {
    process->Resume();
    auto reason = process->WaitOnSignal();
    PrintStopReason(*process, reason);
  } else if (IsPrefix(command, "help")) {
    PrintHelp(args);
  } else {
    fmt::println("Unknown command: {}", command);
  }
}

void MainLoop(std::unique_ptr<ldb::Process>& process) {
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
        HandleCommand(process, line_str);
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
    auto process = Attach(argc, argv);
    MainLoop(process);
  } catch (const ldb::Error& err) {
    fmt::println("{}", err.what());
  }
}