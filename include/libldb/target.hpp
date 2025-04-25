#pragma once

#include <libldb/breakpoint.hpp>
#include <libldb/elf.hpp>
#include <libldb/process.hpp>
#include <libldb/stack.hpp>
#include <memory>

namespace ldb {
class Target {
 public:
  Target() = delete;
  Target(const Target&) = delete;
  Target& operator=(const Target&) = delete;
  Target(Target&&) = delete;
  Target& operator=(Target&&) = delete;

  static std::unique_ptr<Target> Launch(
      std::filesystem::path path,
      std::optional<int> stdoutReplacement = std::nullopt);

  static std::unique_ptr<Target> Attach(pid_t pid);

  Process& GetProcess() { return *process; }
  const Process& GetProcess() const { return *process; }

  Elf& GetElf() { return *elf; }
  const Elf& GetElf() const { return *elf; }

  void NotifyStop(const ldb::StopReason& reason);

  FileAddr GetPcFileAddress() const;

  Stack& GetStack() { return stack; }
  const Stack& GetStack() const { return stack; }

  LineTable::iterator LineEntryAtPc() const;

  StopReason RunUntilAddress(VirtAddr address);

  StopReason StepIn();
  StopReason StepOut();
  StopReason StepOver();

  struct FindFunctionResult {
    std::vector<Die> dwarfFunctions;
    std::vector<std::pair<const Elf*, const Elf64_Sym*>> elfFunctions;
  };

  FindFunctionResult FindFunctions(std::string_view name) const;

  Breakpoint& CreateAdressBreakpoint(VirtAddr address, bool hardware = false,
                                     bool internal = false);
  Breakpoint& CreateFunctionBreakpoint(std::string functionName,
                                       bool hardware = false,
                                       bool internal = false);
  Breakpoint& CreateLineBreakpoint(std::filesystem::path file, std::size_t line,
                                   bool hardware = false,
                                   bool internal = false);

  StoppointCollection<Breakpoint>& Breakpoints() { return breakpoints; }
  const StoppointCollection<Breakpoint>& Breakpoints() const {
    return breakpoints;
  }

 private:
  Target(std::unique_ptr<Process> _process, std::unique_ptr<Elf> _elf)
      : process(std::move(_process)), elf(std::move(_elf)), stack(this) {}

  std::unique_ptr<Process> process;
  std::unique_ptr<Elf> elf;

  Stack stack;

  StoppointCollection<Breakpoint> breakpoints;
};
}  // namespace ldb