#include <elf.h>
#include <fmt/base.h>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <readline/history.h>
#include <readline/readline.h>
#include <spdlog/spdlog.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <iterator>
#include <libldb/breakpoint_site.hpp>
#include <libldb/disassembler.hpp>
#include <libldb/error.hpp>
#include <libldb/libldb.hpp>
#include <libldb/parse.hpp>
#include <libldb/process.hpp>
#include <libldb/register_info.hpp>
#include <libldb/syscalls.hpp>
#include <libldb/target.hpp>
#include <libldb/types.hpp>
#include <span>
#include <sstream>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace {
ldb::Process* LdbProcess = nullptr;

// When ldb process receives SIGINT, stop the inferior process by sending
// SIGSTOP to it.
void HandleSigint(int) { kill(LdbProcess->pid(), SIGSTOP); }
// attach to a process or a program
std::unique_ptr<ldb::Target> Attach(int argc, const char** argv) {
  // Passing PID
  if (argc == 3 && argv[1] == std::string_view("-p")) {
    pid_t pid = std::atoi(argv[2]);
    return ldb::Target::Attach(pid);
  } else {
    // Passing program name.
    // We need to fork a child process to execute the program.
    const char* program_path = argv[1];
    auto target = ldb::Target::Launch(program_path);
    fmt::println("Launched process with PID {}", target->process().pid());
    return target;
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

std::string GetSigtrapInfo(const ldb::Process& process,
                           ldb::StopReason reason) {
  if (reason.trap_reason == ldb::TrapType::SoftwareBreak) {
    auto& site = process.breakpoint_sites().GetByAddress(process.GetPc());
    return fmt::format(" (breakpoint {})", site.id());
  }
  if (reason.trap_reason == ldb::TrapType::HardwareBreak) {
    auto id = process.GetCurrentHardwareStoppoint();

    if (id.index() == 0) {
      return fmt::format(" (breakpoint {})", std::get<0>(id));
    }

    std::string message;
    auto& point = process.watchpoints().GetById(std::get<1>(id));
    message += fmt::format(" (watchpoint {})", point.id());

    if (point.data() == point.previous_data()) {
      message += fmt::format("\nValue: {:#x}", point.data());
    } else {
      message += fmt::format("\nOld value: {:#x}\nNew value: {:#x}",
                             point.previous_data(), point.data());
    }
    return message;
  }
  if (reason.trap_reason == ldb::TrapType::SingleStep) {
    return " (single step)";
  }
  if (reason.trap_reason == ldb::TrapType::Syscall) {
    const auto& info = *reason.syscall_info;
    std::string message = " ";
    if (info.entry) {
      message += "(syscall entry)\n";
      message +=
          fmt::format("syscall: {}({:#x})", ldb::SyscallIdToName(info.id),
                      fmt::join(info.args, ","));
    } else {
      message += "(syscall exit)\n";
      message += fmt::format("syscall returned: {:#x}", info.ret);
    }
    return message;
  }
  return "";
}

std::string GetSignalStopReason(const ldb::Target& target,
                                ldb::StopReason reason) {
  auto& process = target.process();

  auto message = fmt::format("stopped with signal {} at {:#x}",
                             sigabbrev_np(reason.info), process.GetPc().addr());

  // Get the symbol name at the current PC.
  auto& elf = target.elf();
  auto pc = process.GetPc();
  auto func = elf.GetSymbolContainingAddress(pc);
  // If the symbol is a function, add the function name to the message.
  if (func && ELF64_ST_TYPE(func.value()->st_info) == STT_FUNC) {
    message += fmt::format(" ({})", elf.GetString(func.value()->st_name));
  }
  if (reason.info == SIGTRAP) {
    message += GetSigtrapInfo(process, reason);
  }
  return message;
}

void PrintStopReason(const ldb::Target& target, ldb::StopReason reason) {
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
      message = GetSignalStopReason(target, reason);
      break;
      // default:
      //   fmt::println("unknown stop reason");
      //   break;
  }
  fmt::println("Process {} {}", target.process().pid(), message);
}

void PrintDisassembly(ldb::Process& process, ldb::VirtAddr address,
                      std::size_t n_instructions) {
  ldb::Disassembler dis{process};
  auto instructions = dis.Disassemble(n_instructions, address);
  for (const auto& instr : instructions) {
    fmt::println("{:#018x}: {}", instr.address.addr(), instr.text);
  }
}

void HandleStop(ldb::Target& target, ldb::StopReason reason) {
  PrintStopReason(target, reason);
  if (reason.reason == ldb::ProcessState::Stopped) {
    PrintDisassembly(target.process(), target.process().GetPc(), 5);
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
watchpoint  - Commands for operating on watchpoints
catchpoint  - Commands for operating on catchpoints
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
set <address> -h
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
  } else if (IsPrefix(args[1], "watchpoint")) {
    fmt::println(stderr, R"(Available commands:
list
set <address> <write|rw|execute> <size>
enable <id>
disable <id>
delete <id>
)");
  } else if (IsPrefix(args[1], "catchpoint")) {
    fmt::println(stderr, R"(Available commands:
syscall
syscall none
syscall <list of syscall IDs or names>
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
        // 内部断点不显示
        if (site.IsInternal()) {
          return;
        }
        fmt::println("{}: address = {:#x}, {}", site.id(),
                     site.address().addr(),
                     site.IsEnabled() ? "enabled" : "diabled");
      });
    }
    return;
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
    bool hardware = false;
    if (args.size() == 4) {
      if (args[3] == "-h") {
        hardware = true;
      } else {
        ldb::Error::Send("Invalid breakpoint command argument");
      }
    }
    auto& site =
        process.CreateBreakpointSite(ldb::VirtAddr{*address}, hardware);
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

void HandleWatchpointListCommand(ldb::Process& process,
                                 std::span<const std::string> args) {
  auto stoppoint_mode_to_string = [](const auto mode) {
    switch (mode) {
      case ldb::StoppointMode::Execute:
        return "execute";
      case ldb::StoppointMode::ReadWrite:
        return "read_write";
      case ldb::StoppointMode::Write:
        return "write";
      default:
        ldb::Error::Send("Invalid stoppoint mode");
    }
  };
  if (process.watchpoints().Empty()) {
    fmt::println("No watchpoints set");
  } else {
    fmt::println("Current watchpoints:");
    process.watchpoints().ForEach([&](const auto& watchpoint) {
      fmt::println("{}: address = {:#x}, mode = {}, size = {}, {}",
                   watchpoint.id(), watchpoint.address().addr(),
                   stoppoint_mode_to_string(watchpoint.mode()),
                   watchpoint.size(),
                   watchpoint.IsEnabled() ? "enabled" : "disabled");
    });
  }
}
// watchpoint set <address> <mode> <size>
void HandleWatchpointSetCommand(ldb::Process& process,
                                std::span<const std::string> args) {
  if (args.size() != 5) {
    PrintHelp(std::vector<std::string>{"help", "watchpoint"});
    return;
  }

  auto address = ldb::ToIntegral<std::uint64_t>(args[2], 16);
  auto& mode_text = args[3];
  auto size = ldb::ToIntegral<std::size_t>(args[4]);

  if (!address ||
      !(mode_text == "write" || mode_text == "rw" || mode_text == "execute")) {
    PrintHelp(std::vector<std::string>{"help", "watchpoint"});
    return;
  }

  ldb::StoppointMode mode;
  if (mode_text == "write") {
    mode = ldb::StoppointMode::Write;
  } else if (mode_text == "rw") {
    mode = ldb::StoppointMode::ReadWrite;
  } else if (mode_text == "execute") {
    mode = ldb::StoppointMode::Execute;
  }
  process.CreateWatchpoint(ldb::VirtAddr{*address}, mode, *size).Enable();
}

// watchpoint list
// watchpoint set <address> <mode> <size>
// watchpoint enable <id>
// watchpoint disable <id>
// watchpoint delete <id>
void HandleWatchpointCommand(ldb::Process& process,
                             std::span<const std::string> args) {
  if (args.size() < 2) {
    PrintHelp(std::vector<std::string>{"help", "watchpoint"});
    return;
  }

  auto command = args[1];
  if (IsPrefix(command, "list")) {
    HandleWatchpointListCommand(process, args);
    return;
  }
  if (IsPrefix(command, "set")) {
    HandleWatchpointSetCommand(process, args);
    return;
  }

  auto id = ldb::ToIntegral<ldb::Watchpoint::IdType>(args[2]);
  if (!id) {
    fmt::println(stderr, "Command expects watchpoint id");
    return;
  }
  if (args.size() < 3) {
    PrintHelp(std::vector<std::string>{"help", "watchpoint"});
    return;
  }
  if (IsPrefix(command, "enable")) {
    process.watchpoints().GetById(*id).Enable();
  } else if (IsPrefix(command, "disable")) {
    process.watchpoints().GetById(*id).Disable();
  } else if (IsPrefix(command, "delete")) {
    process.watchpoints().RemoveById(*id);
  }
}

void HandleSyscallCatchpointCommand(ldb::Process& process,
                                    std::span<const std::string> args) {
  auto policy = ldb::SyscallCatchPolicy::CatchAll();
  if (args.size() == 3 && args[2] == "none") {
    policy = ldb::SyscallCatchPolicy::CatchNone();
  } else if (args.size() >= 3) {
    auto syscalls = Split(args[2], ',');
    std::vector<int> to_catch;
    to_catch.reserve(syscalls.size());

    std::transform(std::begin(syscalls), std::end(syscalls),
                   std::back_inserter(to_catch), [](const auto& call) {
                     return isdigit(call[0])
                                ? ldb::ToIntegral<int>(call).value()
                                : ldb::SyscallNameToId(call);
                   });
    policy = ldb::SyscallCatchPolicy::CatchSome(std::move(to_catch));
  }
  process.SetSyscallCatchPolicy(std::move(policy));
}

// catchpoint syscall
// catchpoint syscall none
// catchpoint syscall <list>
void HandleCatchpointCommand(ldb::Process& process,
                             std::span<const std::string> args) {
  if (args.size() < 2) {
    PrintHelp(std::vector<std::string>{"help", "catchpoint"});
    return;
  }

  if (IsPrefix(args[1], "syscall")) {
    HandleSyscallCatchpointCommand(process, args);
  }
}

// handle command
void HandleCommand(std::unique_ptr<ldb::Target>& target,
                   std::string_view line) {
  auto args = Split(line, ' ');
  auto command = args[0];
  auto process = &target->process();

  if (IsPrefix(command, "continue")) {
    process->Resume();
    auto reason = process->WaitOnSignal();
    HandleStop(*target, reason);
  } else if (IsPrefix(command, "help")) {
    PrintHelp(args);
  } else if (IsPrefix(command, "register")) {
    HandleRegisterCommand(*process, args);
  } else if (IsPrefix(command, "breakpoint")) {
    HandleBreakpointCommand(*process, args);
  } else if (IsPrefix(command, "step")) {
    auto reason = process->StepInstruction();
    HandleStop(*target, reason);
  } else if (IsPrefix(command, "memory")) {
    HandleMemoryCommand(*process, args);
  } else if (IsPrefix(command, "disassemble")) {
    HandleDisassembleCommand(*process, args);
  } else if (IsPrefix(command, "watchpoint")) {
    HandleWatchpointCommand(*process, args);
  } else if (IsPrefix(command, "catchpoint")) {
    HandleCatchpointCommand(*process, args);
  } else {
    fmt::println("Unknown command: {}", command);
  }
}

void MainLoop(std::unique_ptr<ldb::Target>& target) {
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
        HandleCommand(target, line_str);
      } catch (const ldb::Error& err) {
        fmt::println("{}", err.what());
      }
    }
  }
}
}  // namespace

int main(int argc, const char** argv) {
  spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%s:%# %!] %v");
  if (argc == 1) {
    std::cerr << "No arguments given\n";
    return -1;
  }
  try {
    auto target = Attach(argc, argv);
    LdbProcess = &target->process();
    signal(SIGINT, HandleSigint);
    MainLoop(target);
  } catch (const ldb::Error& err) {
    fmt::println("{}", err.what());
  }
}