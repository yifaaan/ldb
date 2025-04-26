#include <fmt/base.h>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <readline/history.h>
#include <readline/readline.h>
#include <signal.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <libldb/breakpoint.hpp>
#include <libldb/breakpoint_site.hpp>
#include <libldb/disassembler.hpp>
#include <libldb/dwarf.hpp>
#include <libldb/error.hpp>
#include <libldb/parse.hpp>
#include <libldb/process.hpp>
#include <libldb/syscall.hpp>
#include <libldb/target.hpp>
#include <libldb/watchpoint.hpp>
#include <ranges>
#include <string>
#include <vector>

namespace {
ldb::Process* LdbProcess = nullptr;

void HandleSigint(int) { kill(LdbProcess->Pid(), SIGSTOP); }

std::vector<std::string_view> Split(std::string_view sv, char delim) {
  std::vector<std::string_view> splits;
  std::size_t index = 0;
  while (true) {
    auto delimIndex = sv.find(delim, index);
    if (delimIndex != std::string::npos) {
      splits.emplace_back(sv.substr(index, delimIndex - index));
      index = delimIndex + 1;
    } else {
      splits.emplace_back(sv.substr(index));
      break;
    }
  }
  return splits;
}

bool IsPrefix(std::string_view str, std::string_view of) {
  return of.starts_with(str);
}

ldb::Registers::Value ParseRegisterValue(const ldb::RegisterInfo& info,
                                         std::string_view text) {
  try {
    if (info.format == ldb::RegisterFormat::uint) {
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
    } else if (info.format == ldb::RegisterFormat::doubleFloat) {
      return ldb::ToFloat<double>(text).value();
    } else if (info.format == ldb::RegisterFormat::longDouble) {
      return ldb::ToFloat<long double>(text).value();
    } else if (info.format == ldb::RegisterFormat::vector) {
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
  if (reason.trapReason == ldb::TrapType::softwareBreak) {
    auto& site = process.BreakpointSites().GetByAddress(process.GetPc());
    return fmt::format(" (breakpoint {})", site.Id());
  }
  if (reason.trapReason == ldb::TrapType::hardwareBreak) {
    auto id = process.GetCurrentHardwareStoppoint();
    // hardware
    if (id.index() == 0) {
      return fmt::format(" (breakpoint {})", std::get<0>(id));
    }
    // watchpoint
    std::string msg;
    auto& point = process.Watchpoints().GetById(std::get<1>(id));
    msg += fmt::format(" (watchpoint {})", point.Id());
    if (point.Data() == point.PreviousData()) {
      msg += fmt::format("\nValue: {:#x}", point.Data());
    } else {
      msg += fmt::format("\nOld value: {:#x}\nNew value: {:#x}",
                         point.PreviousData(), point.Data());
    }
    return msg;
  }
  if (reason.trapReason == ldb::TrapType::singleStep) {
    return " (single step)";
  }
  if (reason.trapReason == ldb::TrapType::syscall) {
    const auto& info = *reason.syscallInfo;
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
  return {};
}

std::string GetSignalStopReason(const ldb::Target& target,
                                ldb::StopReason reason) {
  auto& process = target.GetProcess();
  auto pc = process.GetPc();
  auto message = fmt::format("stopped with signal {} at {:#x}",
                             sigabbrev_np(reason.info), process.GetPc().Addr());

  // look up the symbol corresponding to the current program counter and print
  // out the function name if there is one.
  // auto func = target.GetElf().GetSymbolContainingAddress(process.GetPc());
  // if (func && ELF64_ST_TYPE(func.value()->st_info) == STT_FUNC) {
  //   // get the function name in strtab
  //   message +=
  //       fmt::format(" ({})",
  //       target.GetElf().GetString(func.value()->st_name));
  // }

  auto line = target.LineEntryAtPc();
  if (line != ldb::LineTable::iterator{}) {
    auto file = line->fileEntry->path.filename().string();
    message += fmt::format(", {}:{}", file, line->line);
  }

  auto func_name = target.FunctionNameAtAddress(pc);
  if (func_name != "") {
    message += fmt::format(" ({})", func_name);
  }
  if (reason.info == SIGTRAP) {
    message += GetSigtrapInfo(process, reason);
  }
  return message;
}

void PrintStopReason(const ldb::Target& target, ldb::StopReason reason) {
  std::string message;
  switch (reason.reason) {
    case ldb::ProcessState::exited:
      message =
          fmt::format("exited with status {}", static_cast<int>(reason.info));
      break;
    case ldb::ProcessState::terminated:
      message =
          fmt::format("terminated with signal {}", sigabbrev_np(reason.info));
      break;
    case ldb::ProcessState::stopped:
      message = GetSignalStopReason(target, reason);
      break;
  }
  fmt::print("Process {} {}\n", target.GetProcess().Pid(), message);
}

void PrintHelp(const std::vector<std::string_view>& args) {
  if (args.size() == 1) {
    std::cerr << R"(Available commands:
continue	- Resume the process
step		- Step over a single instruction
register	- Commands for operating on registers
breakpoint	- Commands for operating on breakpoints
memory		- Commands for operating on memory
disassemble	- Disassemble machine code to assembly
watchpoint	- Commands for operating on watchpoints
catchpoint	- Commands for operating on catchpoints
next		- Step over a single instruction
finish		- Step out of the current function
step		- Step into the next function
stepi		- Step into the next instruction
)";
  } else if (IsPrefix(args[1], "registers")) {
    std::cerr << R"(Available commands:
read
read <register>
read all
write <register> <value>
)";
  } else if (IsPrefix(args[1], "breakpoint")) {
    std::cerr << R"(Available commands:
list
delete <id>
disable <id>
enable <id>
set <address>
set <address> -h
)";
  } else if (IsPrefix(args[1], "memory")) {
    std::cerr << R"(Available commands:
read <address>
read <address> <number of bytes>
write <address> <bytes>
)";
  } else if (IsPrefix(args[1], "disassemble")) {
    std::cerr << R"(Available options:
-c <number of instructions>
-a <start address>
)";
  } else if (IsPrefix(args[1], "watchpoint")) {
    std::cerr << R"(Available options:
list
delete <id>
disable <id>
enable <id>
set <address> <write|rw|execute> <size>
)";
  } else if (IsPrefix(args[1], "catchpoint")) {
    std::cerr << R"(Available options:
syscall
syscall none
syscall <list of syscall IDs or names
)";
  } else {
    std::cerr << "No help available on that\n";
  }
}

void PrintDisassembly(ldb::Process& process, ldb::VirtAddr address,
                      std::size_t nInstructions) {
  ldb::Disassembler dis{process};
  auto instructions = dis.Disassemble(nInstructions, address);
  for (auto& instr : instructions) {
    fmt::print("{:#018x}: {}\n", instr.address.Addr(), instr.text);
  }
}

void PrintSource(const std::filesystem::path& path, std::uint64_t line,
                 std::uint64_t n_lines_context) {
  std::ifstream file{path.string()};
  // auto start_line = line <= n_lines_context ? 1 : line - n_lines_context;
  auto start_line = line;
  auto end_line = line + n_lines_context + 1;

  auto PrintLineStart = [&](auto current_line) {
    auto fill_width = static_cast<int>(std::floor(std::log10(end_line))) + 1;
    auto arrow = current_line == line ? ">" : " ";
    fmt::print("{} {:>{}} ", arrow, current_line, fill_width);
  };

  char c{};
  auto current_line = 1u;
  PrintLineStart(current_line);
  while (current_line <= end_line && file.get(c)) {
    fmt::print("{}", c);

    if (c == '\n') {
      ++current_line;
      PrintLineStart(current_line);
    }
  }
  fmt::println("");
}

void HandleStop(ldb::Target& target, ldb::StopReason reason) {
  PrintStopReason(target, reason);
  if (reason.reason == ldb::ProcessState::stopped) {
    if (target.GetStack().InlineHeight() > 0) {
      auto stack = target.GetStack().InlineStackAtPc();
      auto frame = stack[stack.size() - target.GetStack().InlineHeight()];
      PrintSource(frame.File().path, frame.Line(), 3);
    } else if (auto entry = target.LineEntryAtPc();
               entry != ldb::LineTable::iterator{}) {
      PrintSource(entry->fileEntry->path, entry->line, 3);
    } else {
      PrintDisassembly(target.GetProcess(), target.GetProcess().GetPc(), 5);
    }
  }
}

void HandleRegisterRead(ldb::Process& process,
                        const std::vector<std::string_view>& args) {
  auto format = [](auto t) {
    if constexpr (std::is_floating_point_v<decltype(t)>) {
      return fmt::format("{}", t);
    } else if constexpr (std::is_integral_v<decltype(t)>) {
      return fmt::format("{:#0{}x}", t, sizeof(t) * 2 + 2);
    } else {
      return fmt::format("[{:#04x}]", fmt::join(t, ","));
    }
  };
  if (args.size() == 2 || (args.size() == 3 && args[2] == "all")) {
    for (const auto& info : ldb::registerInfos) {
      auto shouldPrint =
          (args.size() == 3 ||
           info.type == ldb::RegisterType::gpr && info.name != "orig_rax");
      if (!shouldPrint) continue;
      auto value = process.GetRegisters().Read(info);
      fmt::print("{}:\t{}\n", info.name, std::visit(format, value));
    }
  } else if (args.size() == 3) {
    try {
      auto info = ldb::RegisterInfoByName(args[2]);
      auto value = process.GetRegisters().Read(info);
      fmt::print("{}:\t{}\n", info.name, std::visit(format, value));
    } catch (ldb::Error& err) {
      std::cerr << "No such register\n";
      return;
    }
  } else {
    PrintHelp({"help", "register"});
  }
}

void HandleRegisterWrite(ldb::Process& process,
                         const std::vector<std::string_view>& args) {
  if (args.size() != 4) {
    PrintHelp({"help", "register"});
    return;
  }
  try {
    auto info = ldb::RegisterInfoByName(args[2]);
    auto value = ParseRegisterValue(info, args[3]);
    process.GetRegisters().Write(info, value);
  } catch (ldb::Error& err) {
    std::cerr << err.what() << '\n';
    return;
  }
}

void HandleRegisterCommand(ldb::Process& process,
                           const std::vector<std::string_view>& args) {
  if (args.size() < 2) {
    PrintHelp({"help", "register"});
    return;
  }

  if (IsPrefix(args[1], "read")) {
    HandleRegisterRead(process, args);
  } else if (IsPrefix(args[1], "write")) {
    HandleRegisterWrite(process, args);
  } else {
    PrintHelp({"help", "register"});
  }
}

void HandleBreakpointListCommand(ldb::Target& target) {
  if (target.Breakpoints().Empty()) {
    fmt::print("No breakpoints set\n");
    return;
  }
  // Current breakpoints:
  // 1: address = 0x5555555551ac, enabled:
  //    .1: address = 0x5555555551ac, enabled
  // 2: function = main, enabled:
  //    .2: address = 0x5555555551a2, enabled
  // 3: file = force_inline.cpp, line = 15, enabled:
  //    .3: address = 0x555555555197, enabled
  //    .4: address = 0x5555555551b3, enabled
  fmt::print("Current breakpoints:\n");
  target.Breakpoints().ForEach([](const auto& bp) {
    if (bp.IsInternal()) return;
    fmt::print("{}: ", bp.Id());
    if (auto func_bp = dynamic_cast<const ldb::FunctionBreakpoint*>(&bp)) {
      fmt::print("function = {}", func_bp->FunctionName());
    } else if (auto line_bp = dynamic_cast<const ldb::LineBreakpoint*>(&bp)) {
      fmt::print("file = {}, line = {}", line_bp->File().string(),
                 line_bp->Line());
    } else if (auto addr_bp =
                   dynamic_cast<const ldb::AddressBreakpoint*>(&bp)) {
      fmt::print("address = {:#x}", addr_bp->Address().Addr());
    }
    fmt::print(", {}:\n", bp.IsEnabled() ? "enabled" : "disabled");
    bp.BreakpointSites().ForEach([](const auto& site) {
      fmt::print("    .{}: address = {:#x}, {}\n", site.Id(),
                 site.Address().Addr(),
                 site.IsEnabled() ? "enabled" : "disabled");
    });
  });
}

// break set <function name>
// break set <file>:<line>
// break set 0x<address>
void HandleBreakpointSetCommand(ldb::Target& target,
                                std::span<std::string_view> args) {
  bool hardware = false;
  if (args.size() == 4) {
    if (args[3] == "-h")
      hardware = true;
    else
      ldb::Error::Send("Invalid breakpoint command argument");
  }

  if (args[2].starts_with("0x")) {
    auto address = ldb::ToIntegral<std::uint64_t>(args[2]);
    if (!address) {
      fmt::print(stderr,
                 "Breakpoint command expects address in hexadecimal, prefied "
                 "with '0x'\n");
      return;
    }
    target.CreateAdressBreakpoint(ldb::VirtAddr{*address}, hardware).Enable();
  } else if (args[2].find(':') != std::string::npos) {
    auto file_line = Split(args[2], ':');
    auto file = file_line[0];
    auto line = ldb::ToIntegral<std::uint64_t>(file_line[1]);
    if (!line) {
      fmt::print(stderr, "Line number should be an integer\n");
      return;
    }
    target.CreateLineBreakpoint(file, *line).Enable();
  } else {
    target.CreateFunctionBreakpoint(std::string{args[2]}).Enable();
  }
}

// E.g.
// Breakpoint: break enable 1.
// Breakpoint site: break enable 1.2.
void HandleBreakpointToggle(ldb::Target& target,
                            std::span<std::string_view> args) {
  auto command = args[1];
  auto dot_pos = args[2].find('.');
  auto id_str = args[2].substr(0, dot_pos);
  auto id = ldb::ToIntegral<ldb::Breakpoint::IdType>(id_str);
  if (!id) {
    fmt::print(stderr, "Command expects breakpoint id\n");
    return;
  }
  auto& bp = target.Breakpoints().GetById(*id);
  if (dot_pos != std::string::npos) {
    // breakpoint site
    auto site_id_str = args[2].substr(dot_pos + 1);
    auto site_id = ldb::ToIntegral<ldb::BreakpointSite::IdType>(site_id_str);
    if (!site_id) {
      fmt::print(stderr, "Command expects breakpoint site id\n");
      return;
    }
    if (IsPrefix(command, "enable")) {
      bp.BreakpointSites().GetById(*site_id).Enable();
    } else if (IsPrefix(command, "disable")) {
      bp.BreakpointSites().GetById(*site_id).Disable();
    }
  } else if (IsPrefix(command, "enable")) {
    bp.Enable();
  } else if (IsPrefix(command, "disable")) {
    bp.Disable();
  } else if (IsPrefix(command, "delete")) {
    // all sites
    bp.BreakpointSites().ForEach([&target](auto& site) {
      target.GetProcess().BreakpointSites().RemoveByAddress(site.Address());
    });
    // breakpoint
    target.Breakpoints().RemoveById(*id);
  }
}

void HandleBreakpointCommand(ldb::Target& target,
                             std::span<std::string_view> args) {
  if (args.size() < 2) {
    PrintHelp({"help", "breakpoint"});
    return;
  }
  auto command = args[1];
  if (IsPrefix(command, "list")) {
    HandleBreakpointListCommand(target);
    return;
  }

  if (args.size() < 3) {
    PrintHelp({"help", "breakpoint"});
    return;
  }

  if (IsPrefix(command, "set")) {
    HandleBreakpointSetCommand(target, args);
    return;
  }

  HandleBreakpointToggle(target, args);
}

// memory read <address>
// memory read <address> <number of bytes>
void HandleMemoryReadCommand(ldb::Process& process,
                             std::span<std::string_view> args) {
  auto address = ldb::ToIntegral<std::uint64_t>(args[2], 16);
  if (!address) {
    ldb::Error::Send("Invalid address format");
  }
  // default to read 32 bytes
  auto nBytes = 32;
  if (args.size() == 4) {
    auto bytesArg = ldb::ToIntegral<std::size_t>(args[3]);
    if (!bytesArg) {
      ldb::Error::Send("Invalid number of bytes");
    }
    nBytes = *bytesArg;
  }
  auto data = process.ReadMemory(ldb::VirtAddr{*address}, nBytes);
  for (std::size_t i = 0; i < data.size(); i += 16) {
    auto start = data.begin() + i;
    auto end = data.begin() + std::min(i + 16, data.size());
    ;
    fmt::print("{:#016x}: {:02x}\n", *address + i, fmt::join(start, end, " "));
  }
}

// memory write <address> <values>
// mem write 0x555555555156 [0xff,0xff]
void HandleMemoryWriteCommand(ldb::Process& process,
                              std::span<std::string_view> args) {
  if (args.size() != 4) {
    PrintHelp({"help", "memory"});
    return;
  }
  auto address = ldb::ToIntegral<std::uint64_t>(args[2], 16);
  if (!address) {
    ldb::Error::Send("Invalid address format");
  }
  auto data = ldb::ParseVector(args[3]);
  process.WriteMemory(ldb::VirtAddr{*address}, data);
}

void HandleMemoryCommand(ldb::Process& process,
                         std::span<std::string_view> args) {
  if (args.size() < 3) {
    PrintHelp({"help", "memory"});
    return;
  }
  if (IsPrefix(args[1], "read")) {
    HandleMemoryReadCommand(process, args);
  } else if (IsPrefix(args[1], "write")) {
    HandleMemoryWriteCommand(process, args);
  } else {
    PrintHelp({"help", "memory"});
  }
}

// disassemble -c <n_instructions> -a <address>
void HandleDisassembleCommand(ldb::Process& process,
                              std::span<std::string_view> args) {
  auto address = process.GetPc();
  std::size_t nInstructions = 5;
  auto it = args.begin() + 1;
  while (it != args.end()) {
    if (*it == "-a" && it + 1 != args.end()) {
      ++it;
      auto optAddr = ldb::ToIntegral<std::uint64_t>(*it++, 16);
      if (!optAddr) {
        ldb::Error::Send("Invalid address format");
      }
      address = ldb::VirtAddr{*optAddr};
    } else if (*it == "-c" && it + 1 != args.end()) {
      ++it;
      auto optN = ldb::ToIntegral<std::size_t>(*it++);
      if (!optN) {
        ldb::Error::Send("Invalid instruction count");
      }
      nInstructions = *optN;
    } else {
      PrintHelp({"help", "disassemble"});
      return;
    }
  }
  PrintDisassembly(process, address, nInstructions);
}

void HandleWatchpointList(ldb::Process& process,
                          std::span<std::string_view> args) {
  auto StoppointModeToString = [](auto mode) {
    switch (mode) {
      case ldb::StoppointMode::execute:
        return "execute";
      case ldb::StoppointMode::readWrite:
        return "read/write";
      case ldb::StoppointMode::write:
        return "write";
      default:
        ldb::Error::Send("Invalid stoppoint mode");
    }
  };
  if (process.Watchpoints().Empty()) {
    fmt::print("No watchpoints set\n");
  } else {
    fmt::print("Current watchpoints:\n");
    process.Watchpoints().ForEach([&](const auto& watchpoint) {
      fmt::print("{}: address = {:#x}, mode = {}, size = {}, {}\n",
                 watchpoint.Id(), watchpoint.Address().Addr(),
                 StoppointModeToString(watchpoint.Mode()), watchpoint.Size(),
                 watchpoint.IsEnabled() ? "enabled" : "disabled");
    });
  }
}

void HandleWatchpointSet(ldb::Process& process,
                         std::span<std::string_view> args) {
  if (args.size() != 5) {
    PrintHelp({"help", "watchpoint"});
    return;
  }
  auto address = ldb::ToIntegral<std::uint64_t>(args[2], 16);
  auto modeTxt = args[3];
  auto size = ldb::ToIntegral<std::size_t>(args[4]);
  if (!address || !size ||
      (modeTxt != "write" && modeTxt != "rw" && modeTxt != "execute")) {
    PrintHelp({"help", "watchpoint"});
    return;
  }
  ldb::StoppointMode mode;
  if (modeTxt == "write")
    mode = ldb::StoppointMode::write;
  else if (modeTxt == "rw")
    mode = ldb::StoppointMode::readWrite;
  else if (modeTxt == "execute")
    mode = ldb::StoppointMode::execute;

  process.CreateWatchpoint(ldb::VirtAddr{*address}, mode, *size).Enable();
}

// watchpoint list
// watchpoint set <address> <mode> <size>
// watchpoint enable <id>
// watchpoint disable <id>
// watchpoint delete <id>
void HandleWatchpointCommand(ldb::Process& process,
                             std::span<std::string_view> args) {
  if (args.size() < 2) {
    PrintHelp({"help", "watchpoint"});
    return;
  }
  auto command = args[1];
  if (IsPrefix(command, "list")) {
    HandleWatchpointList(process, args);
    return;
  } else if (IsPrefix(command, "set")) {
    HandleWatchpointSet(process, args);
    return;
  }

  if (args.size() < 3) {
    PrintHelp({"help", "watchpoint"});
    return;
  }
  auto id = ldb::ToIntegral<ldb::Watchpoint::IdType>(args[2]);
  if (!id) {
    std::cerr << "Command expects watchpoint id\n";
    return;
  }

  if (IsPrefix(command, "enable")) {
    process.Watchpoints().GetById(*id).Enable();
  } else if (IsPrefix(command, "disable")) {
    process.Watchpoints().GetById(*id).Disable();
  } else if (IsPrefix(command, "delete")) {
    process.Watchpoints().RemoveById(*id);
  }
}

void HandleSyscallCatchpointCommand(ldb::Process& process,
                                    std::span<std::string_view> args) {
  auto policy = ldb::SyscallCatchPolicy::CatchAll();
  if (args.size() == 3 && args[2] == "none") {
    policy = ldb::SyscallCatchPolicy::CatchNone();
  } else if (args.size() >= 3) {
    auto syscalls = Split(args[2], ',');

    auto toCatch = syscalls | std::views::transform([](auto syscall) {
                     return isdigit(syscall[0])
                                ? ldb::ToIntegral<int>(syscall).value()
                                : ldb::SyscallNameToId(syscall);
                   });
    policy = ldb::SyscallCatchPolicy::CatchSome(
        std::vector<int>(toCatch.begin(), toCatch.end()));
  }
  process.SetSyscallCatchPolicy(std::move(policy));
}

// catchpoint syscall
// catchpoint syscall none
// catchpoint syscall <list>
void HandleCatchpointCommand(ldb::Process& process,
                             std::span<std::string_view> args) {
  if (args.size() < 2) {
    PrintHelp({"help", "catchpoint"});
    return;
  }
  if (IsPrefix(args[1], "syscall")) {
    HandleSyscallCatchpointCommand(process, args);
  }
}

void HandleCommand(std::unique_ptr<ldb::Target>& target,
                   std::string_view line) {
  auto args = Split(line, ' ');
  auto command = args[0];

  auto process = &target->GetProcess();
  if (IsPrefix(command, "continue")) {
    process->Resume();
    auto reason = process->WaitOnSignal();
    HandleStop(*target, reason);
  } else if (IsPrefix(command, "help")) {
    PrintHelp(args);
  } else if (IsPrefix(command, "register")) {
    HandleRegisterCommand(*process, args);
  } else if (IsPrefix(command, "breakpoint")) {
    HandleBreakpointCommand(*target, args);
  }
  // else if (IsPrefix(command, "step")) {
  //   auto reason = process->StepInstruction();
  //   HandleStop(*target, reason);
  // }
  else if (IsPrefix(command, "memory")) {
    HandleMemoryCommand(*process, args);
  } else if (IsPrefix(command, "disassemble")) {
    HandleDisassembleCommand(*process, args);
  } else if (IsPrefix(command, "watchpoint")) {
    HandleWatchpointCommand(*process, args);
  } else if (IsPrefix(command, "catchpoint")) {
    HandleCatchpointCommand(*process, args);
  } else if (IsPrefix(command, "next")) {
    auto reason = target->StepOver();
    HandleStop(*target, reason);
  } else if (IsPrefix(command, "finish")) {
    auto reason = target->StepOut();
    HandleStop(*target, reason);
  } else if (IsPrefix(command, "step")) {
    auto reason = target->StepIn();
    HandleStop(*target, reason);
  } else if (IsPrefix(command, "stepi")) {
    auto reason = process->StepInstruction();
    HandleStop(*target, reason);
  } else {
    std::cerr << "Unknown command\n";
  }
}

std::unique_ptr<ldb::Target> Attach(int argc, const char** argv) {
  if (argc == 3 && argv[1] == std::string_view{"-p"}) {
    pid_t pid = std::atoi(argv[2]);
    return ldb::Target::Attach(pid);
  } else {
    const char* programPath = argv[1];
    auto target = ldb::Target::Launch(programPath);
    fmt::print("Launched process with PID {}\n", target->GetProcess().Pid());
    return target;
  }
}
}  // namespace

void MainLoop(std::unique_ptr<ldb::Target>& target) {
  // handle user input command
  char* line = nullptr;
  while ((line = readline("ldb> ")) != nullptr) {
    std::string lineString;
    if (line == std::string_view{""}) {
      free(line);
      // empty line: re-run the last command if it exists
      if (history_length > 0) {
        lineString = history_list()[history_length - 1]->line;
      }
    } else {
      lineString = line;
      add_history(line);
      free(line);
    }
    if (!lineString.empty()) {
      // string converts to string_view because of the conversion function
      // [std::string::operator std::string_view()]
      try {
        HandleCommand(target, lineString);
      } catch (const ldb::Error& err) {
        std::cout << err.what() << '\n';
      }
    }
  }
}

int main(int argc, const char** argv) {
  if (argc == 1) {
    std::cerr << "No arguments given\n";
    return -1;
  }

  try {
    auto target = Attach(argc, argv);
    LdbProcess = &target->GetProcess();
    signal(SIGINT, HandleSigint);
    MainLoop(target);
  } catch (const ldb::Error& err) {
    std::cout << err.what() << '\n';
  }
}