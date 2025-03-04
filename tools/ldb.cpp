#include <fmt/base.h>
#include <fmt/format.h>
#include <fmt/ranges.h>
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
#include <libldb/parse.hpp>
#include <libldb/process.hpp>
#include <libldb/register_info.hpp>
#include <span>
#include <sstream>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

#include "libldb/breakpoint_site.hpp"
#include "libldb/disassembler.hpp"
#include "libldb/types.hpp"

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
    auto process = ldb::Process::Launch(program_path);
    fmt::println("Launched process with PID {}", process->pid());
    return process;
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

ldb::Registers::Value ParseRegisterValue(const ldb::RegisterInfo& info,
                                         std::string_view text) {
  try {
    if (info.format == ldb::RegisterFormat::Uint) {
      switch (info.size) {
        case 1:
          return ldb::ToIntegral<std::uint8_t>(text, 16).value();
        case 2:
          return ldb::ToIntegral<std::uint16_t>(text, 16).value();
        case 4:
          return ldb::ToIntegral<std::uint32_t>(text, 16).value();
        case 8:
          return ldb::ToIntegral<std::uint64_t>(text, 16).value();
      }
    } else if (info.format == ldb::RegisterFormat::LongDouble) {
      return ldb::ToFloat<long double>(text).value();
    } else if (info.format == ldb::RegisterFormat::Vector) {
      if (info.size == 8) {
        return ldb::ParseVector<8>(text);
      } else if (info.size == 16) {
        return ldb::ParseVector<16>(text);
      }
    }
  } catch (...) {
  }
  ldb::Error::Send("Invalid format");
}

void PrintStopReason(const ldb::Process& process, ldb::StopReason reason) {
  std::string message;
  switch (reason.reason) {
    case ldb::ProcessState::Exited:
      message =
          fmt::format("exited with status {}", static_cast<int>(reason.info));
      break;
    case ldb::ProcessState::Terminated:
      message =
          fmt::format("terminated with signal {}", sigabbrev_np(reason.info));
      break;
    case ldb::ProcessState::Stopped:
      message = fmt::format("stopped with signal {} at {:#x}",
                            sigabbrev_np(reason.info), process.GetPc().addr());
      break;
      // default:
      //   fmt::println("unknown stop reason");
      //   break;
  }
  fmt::println("Process {} {}", process.pid(), message);
}

void PrintDisassembly(ldb::Process& process, ldb::VirtAddr address,
                      std::size_t n_instructions) {
  ldb::Disassembler dis{process};
  auto instructions = dis.Disassemble(n_instructions, address);
  for (const auto& instr : instructions) {
    fmt::println("{:#018x}: {}", instr.address.addr(), instr.text);
  }
}

void HandleStop(ldb::Process& process, ldb::StopReason reason) {
  PrintStopReason(process, reason);
  if (reason.reason == ldb::ProcessState::Stopped) {
    PrintDisassembly(process, process.GetPc(), 5);
  }
}

// help ...
void PrintHelp(std::span<const std::string> args) {
  if (args.size() == 1) {
    fmt::println(stderr, R"(Available commands:
continue    - Resume the process
register    - Commands for operating on registers
breakpoint  - Commands for operating on breakpoints
step        - Step over a single instruction
memory      - Commands for operating on memory
disassemble - Disassemble machine code to assembly
)");
  } else if (IsPrefix(args[1], "register")) {
    fmt::println(stderr, R"(Available commands:
read
read <register>
read all
write <register> <value>
)");
  } else if (IsPrefix(args[1], "breakpoint")) {
    fmt::println(stderr, R"(Available commands:
list
delete <id>
enable <id>
disable <id>
set <address>
)");
  } else if (IsPrefix(args[1], "memory")) {
    fmt::println(stderr, R"(Available commands:
read <address>
read <address> <numner of bytes>
write <address> <bytes>
)");
  } else if (IsPrefix(args[1], "disassemble")) {
    fmt::println(stderr, R"(Available commands:
-c <number of instructions>
-a <start address>
)");
  } else {
    fmt::println(stderr, "No help available on that");
  }
}

void HandleRegisterRead(ldb::Process& process,
                        std::span<const std::string> args) {
  auto format = [](const auto t) {
    if constexpr (std::is_floating_point_v<decltype(t)>) {
      return fmt::format("{}", t);
    } else if constexpr (std::is_integral_v<decltype(t)>) {
      return fmt::format("{:#0{}x}", t, sizeof(t) * 2 + 2);
    } else {
      // For vector registers, print the hex values of the elements.
      // For example, <1, 2, 3> -> [0x01 0x02 0x03]
      return fmt::format("[{:#04x}]", fmt::join(t, ","));
    }
  };
  if (args.size() == 2 or (args.size() == 3 && args[2] == "all")) {
    for (const auto& reg_info : ldb::RegisterInfos) {
      auto should_print =
          (reg_info.type == ldb::RegisterType::Gpr || args.size() == 3) &&
          reg_info.name != "orig_rax";
      if (!should_print) continue;
      auto value = process.registers().Read(reg_info);
      fmt::println("{}:\t{}", reg_info.name, std::visit(format, value));
    }
  } else if (args.size() == 3) {
    try {
      auto reg_info = ldb::RegisterInfoByName(args[2]);
      auto value = process.registers().Read(reg_info);
      fmt::println("{}:\t{}", reg_info.name, std::visit(format, value));
    } catch (const ldb::Error& err) {
      fmt::println(stderr, "No such register");
      return;
    }
  } else {
    PrintHelp(std::vector<std::string>{"help", "register"});
  }
}

void HandleRegisterWrite(ldb::Process& process,
                         std::span<const std::string> args) {
  if (args.size() != 4) {
    PrintHelp(std::vector<std::string>{"help", "register"});
    return;
  }

  try {
    auto reg_info = ldb::RegisterInfoByName(args[2]);
    auto value = ParseRegisterValue(reg_info, args[3]);
    process.registers().Write(reg_info, value);
  } catch (const ldb::Error& err) {
    fmt::println("{}", err.what());
    return;
  }
}

void HandleRegisterCommand(ldb::Process& process,
                           std::span<const std::string> args) {
  if (args.size() < 2) {
    PrintHelp(std::vector<std::string>{"help", "register"});
    return;
  }

  if (IsPrefix(args[1], "read")) {
    HandleRegisterRead(process, args);
  } else if (IsPrefix(args[1], "write")) {
    HandleRegisterWrite(process, args);
  } else {
    PrintHelp(std::vector<std::string>{"help", "register"});
  }
}

void HandleBreakpointCommand(ldb::Process& process,
                             std::span<const std::string> args) {
  if (args.size() < 2) {
    PrintHelp(std::vector<std::string>{"help", "breakpoint"});
    return;
  }

  auto command = args[1];

  // List all breakpoints.
  if (IsPrefix(command, "list")) {
    if (process.breakpoint_sites().Empty()) {
      fmt::println("No breakpoints set");
    } else {
      fmt::println("Current breakpoints:");
      process.breakpoint_sites().ForEach([](const auto& site) {
        fmt::println("{}: address = {:#x}, {}", site.id(),
                     site.address().addr(),
                     site.IsEnabled() ? "enabled" : "diabled");
      });
    }
  }

  if (args.size() < 3) {
    PrintHelp(std::vector<std::string>{"help", "breakpoint"});
    return;
  }

  // Set a breakpoint at the given address.
  if (IsPrefix(command, "set")) {
    auto address = ldb::ToIntegral<std::uint64_t>(args[2], 16);
    if (!address) {
      fmt::println(stderr,
                   "Breakpoint command expectes address in hexadecimal, prefix "
                   "with '0x'");
      return;
    }
    auto& site = process.CreateBreakpointSite(ldb::VirtAddr{*address});
    site.Enable();
    return;
  }

  auto id = ldb::ToIntegral<ldb::BreakpointSite::IdType>(args[2]);
  if (!id) {
    fmt::println(stderr, "Command expects breakpoint id");
    return;
  }

  if (IsPrefix(command, "enable")) {
    process.breakpoint_sites().GetById(*id).Enable();
  } else if (IsPrefix(command, "disable")) {
    process.breakpoint_sites().GetById(*id).Disable();
  } else if (IsPrefix(command, "delete")) {
    process.breakpoint_sites().RemoveById(*id);
  }
}

// sdb> mem read 0x000055555555515b
// 0x0055555555515b: cc f0 fe ff ff b8 00 00 00 00 5d c3 00 f3 0f 1e
// 0x0055555555516b: fa 48 83 ec 08 48 83 c4 08 c3 00 00 00 00 00 00
void HandleMemoryReadCommand(ldb::Process& process,
                             std::span<const std::string> args) {
  auto address = ldb::ToIntegral<std::uint64_t>(args[2], 16);
  if (!address) {
    ldb::Error::Send("Invalid address format");
    return;
  }

  std::size_t n_bytes = 32;
  if (args.size() == 4) {
    auto bytes_arg = ldb::ToIntegral<std::size_t>(args[3]);
    if (!bytes_arg) {
      ldb::Error::Send("Invalid number of bytes");
      n_bytes = *bytes_arg;
    }
  }

  auto data = process.ReadMemory(ldb::VirtAddr{*address}, n_bytes);
  for (std::size_t i = 0; i < data.size(); i += 16) {
    auto start = data.begin() + i;
    auto end = data.begin() + std::min(i + 16, data.size());
    fmt::println("{:#016x}: {:02x}", *address + i, fmt::join(start, end, " "));
  }
}
// mem write 0x555555555156 [0xff,0xff]
void HandleMemoryWriteCommand(ldb::Process& process,
                              std::span<const std::string> args) {
  if (args.size() != 4) {
    PrintHelp(std::vector<std::string>{"help", "memory"});
    return;
  }

  auto address = ldb::ToIntegral<std::uint64_t>(args[2], 16);
  if (!address) {
    ldb::Error::Send("Invalid address format");
  }
  auto data = ldb::ParseVector(args[3]);
  process.WriteMemory(ldb::VirtAddr{*address}, {std::begin(data), data.size()});
}

void HandleMemoryCommand(ldb::Process& process,
                         std::span<const std::string> args) {
  if (args.size() < 3) {
    PrintHelp(std::vector<std::string>{"help", "memory"});
    return;
  }

  auto command = args[1];
  if (IsPrefix(command, "read")) {
    HandleMemoryReadCommand(process, args);
  } else if (IsPrefix(command, "write")) {
    HandleMemoryWriteCommand(process, args);
  } else {
    PrintHelp(std::vector<std::string>{"help", "memory"});
  }
}

// disassemble -c <n_instructions> -a <address>
void HandleDisassembleCommand(ldb::Process& process,
                              std::span<const std::string> args) {
  std::size_t n_instructions = 5;
  auto address = process.GetPc();

  auto it = args.begin() + 1;
  while (it != std::end(args)) {
    if (*it == "-a" && it + 1 != std::end(args)) {
      it++;
      auto opt_addr = ldb::ToIntegral<std::uint64_t>(*it++, 16);
      if (!opt_addr) {
        ldb::Error::Send("Invalid address format");
      }
      address = ldb::VirtAddr{*opt_addr};
    } else if (*it == "-c" && it + 1 != std::end(args)) {
      it++;
      auto opt_n = ldb::ToIntegral<std::size_t>(*it++);
      if (!opt_n) {
        ldb::Error::Send("Invalid number of instructions");
        n_instructions = *opt_n;
      } else {
        PrintHelp(std::vector<std::string>{"help", "disassemble"});
        return;
      }
    }
  }

  PrintDisassembly(process, address, n_instructions);
}

// handle command
void HandleCommand(std::unique_ptr<ldb::Process>& process,
                   std::string_view line) {
  auto args = Split(line, ' ');
  auto command = args[0];

  if (IsPrefix(command, "continue")) {
    process->Resume();
    auto reason = process->WaitOnSignal();
    HandleStop(*process, reason);
  } else if (IsPrefix(command, "help")) {
    PrintHelp(args);
  } else if (IsPrefix(command, "register")) {
    HandleRegisterCommand(*process, args);
  } else if (IsPrefix(command, "breakpoint")) {
    HandleBreakpointCommand(*process, args);
  } else if (IsPrefix(command, "step")) {
    auto reason = process->StepInstruction();
    HandleStop(*process, reason);
  } else if (IsPrefix(command, "memory")) {
    HandleMemoryCommand(*process, args);
  } else if (IsPrefix(command, "disassemble")) {
    HandleDisassembleCommand(*process, args);
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