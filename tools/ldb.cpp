#include <cstdio>
#include <readline/history.h>
#include <readline/readline.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>


#include <iostream>
#include <libldb/libldb.hpp>
#include <string>

namespace {
// attach to a process or a program
pid_t attach(int argc, const char** argv) {
  pid_t pid = 0;
  // Passing PID
  if (argc == 3 && argv[1] == std::string_view("-p")) {
    pid = std::stoi(argv[2]);
    if (pid <= 0) {
      std::cerr << "Invalid PID: " << argv[2] << "\n";
      return -1;
    }
    // Attach to the process.
    // It will send SIGSTOP to the process to pause it.
    if (ptrace(PTRACE_ATTACH, pid, /*addr*/ nullptr, /*data*/ nullptr) < 0) {
      std::perror("Could not attach");
      return -1;
    } else {
      // Passing program name.
      // We need to fork a child process to execute the program.
      const char* program_path = argv[1];
      if ((pid = fork()) < 0) {
        std::perror("fork failed");
        return -1;
      }
      if (pid == 0) {
        // `PTRACE_TRACEME`的作用
        // 子进程调用`PTRACE_TRACEME`后，内核会在其`task_struct`中设置`PT_PTRACED`标志，标记该进程为被跟踪状态。
        // 此时子进程不会主动停止，但后续的`execve()`系统调用会触发内核的调试拦截机制。
        if (ptrace(PTRACE_TRACEME, 0, nullptr, nullptr) < 0) {
          std::perror("Tracing failed");
          return -1;
        }
        // Execute the program.
        // Arguments list must be terminated by nullptr.
        // `execve()`的暂停行为
        // 当子进程调用`execve()`加载新程序时，内核会在新程序入口点（如`_start`）执行前插入一个调试陷阱，由内核自动发送`SIGTRAP`信号给子进程。
        // 子进程因`SIGTRAP`进入`TASK_STOPPED`状态，此时父进程需要通过`waitpid()`同步这一状态变更。
        if (execlp(program_path, program_path, nullptr) < 0) {
          std::perror("Exec failed");
          return -1;
        }
      }
    }
  }
  return pid;
}

// handle command
void handle_command(pid_t pid, std::string_view line) {
  std::cout << "handle_command: " << line << "\n";
}
}  // namespace

int main(int argc, const char** argv) {
  if (argc == 1) {
    std::cerr << "No arguments given\n";
    return -1;
  }
  pid_t pid = attach(argc, argv);

  // After attaching to the process, we need to wait for the process to stop.
  // And then we can accept commands from the user.
  int wait_status;
  int options = 0;
  if (waitpid(pid, &wait_status, options) < 0) {
    std::perror("wailpid failed");
  }

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
      handle_command(pid, line_str);
    }
  }
}