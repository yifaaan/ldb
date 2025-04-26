#include "libldb/target.hpp"

#include <elf.h>
#include <signal.h>

#include <format>

#include "libldb/breakpoint.hpp"
#include "libldb/disassembler.hpp"

namespace {
std::unique_ptr<ldb::Elf> CreateLoadedElf(const ldb::Process& process,
                                          const std::filesystem::path& path) {
  auto auxv = process.GetAuxv();
  auto obj = std::make_unique<ldb::Elf>(path);
  // set load bias
  obj->NotifyLoaded(ldb::VirtAddr{auxv[AT_ENTRY] - obj->GetHeader().e_entry});
  return obj;
}
}  // namespace

namespace ldb {
std::unique_ptr<Target> Target::Launch(std::filesystem::path path,
                                       std::optional<int> stdoutReplacement) {
  auto process = Process::Launch(path, true, stdoutReplacement);
  auto obj = CreateLoadedElf(*process, path);
  auto target =
      std::unique_ptr<Target>(new Target{std::move(process), std::move(obj)});
  target->GetProcess().SetTarget(target.get());
  return target;
}

std::unique_ptr<Target> Target::Attach(pid_t pid) {
  auto path = std::format("/proc/{}/exe", pid);
  auto process = Process::Attach(pid);
  auto obj = CreateLoadedElf(*process, path);
  auto target =
      std::unique_ptr<Target>(new Target{std::move(process), std::move(obj)});
  target->GetProcess().SetTarget(target.get());
  return target;
}

void Target::NotifyStop(const ldb::StopReason& reason) {
  stack.ResetInlineHeight();
}

FileAddr Target::GetPcFileAddress() const {
  return process->GetPc().ToFileAddr(*elf);
}

LineTable::iterator Target::LineEntryAtPc() const {
  auto pc = GetPcFileAddress();
  if (!pc.ElfFile()) return {};
  auto cu = pc.ElfFile()->GetDwarf().CompileUnitContainingAddress(pc);
  if (!cu) return {};
  return cu->Lines().GetEntryByAddress(pc);
}

StopReason Target::RunUntilAddress(VirtAddr address) {
  BreakpointSite* breakpointToRemove = nullptr;
  if (!process->BreakpointSites().ContainsAddress(address)) {
    breakpointToRemove = &process->CreateBreakpointSite(address, false, true);
    breakpointToRemove->Enable();
  }
  process->Resume();
  auto reason = process->WaitOnSignal();
  if (reason.IsBreakpoint() && process->GetPc() == address) {
    reason.trapReason = TrapType::singleStep;
  }
  if (breakpointToRemove) {
    process->BreakpointSites().RemoveByAddress(address);
  }
  return reason;
}

StopReason Target::StepIn() {
  auto& stack = GetStack();
  if (stack.InlineHeight() > 0) {
    stack.SimulateInlinedStepIn();
    return StopReason{ProcessState::stopped, SIGTRAP, TrapType::singleStep};
  }
  auto origLine = LineEntryAtPc();
  do {
    auto reason = process->StepInstruction();
    if (!reason.IsStep()) {
      return reason;
    }
  } while ((LineEntryAtPc() == origLine || LineEntryAtPc()->endSequence) &&
           LineEntryAtPc() != LineTable::iterator{});
  auto pc = GetPcFileAddress();
  if (pc.ElfFile()) {
    auto& dwarf = pc.ElfFile()->GetDwarf();
    auto func = dwarf.FunctionContainingAddress(pc);
    if (func && func->LowPc() == pc) {
      auto line = LineEntryAtPc();
      if (line != LineTable::iterator{}) {
        ++line;
        return RunUntilAddress(line->address.ToVirtAddr());
      }
    }
  }
  return StopReason{ProcessState::stopped, SIGTRAP, TrapType::singleStep};
}

StopReason Target::StepOut() {
  auto& stack = GetStack();
  auto inlineStack = stack.InlineStackAtPc();
  auto hasInlineFrames = inlineStack.size() > 1;
  auto atInlineFrame = stack.InlineHeight() < inlineStack.size() - 1;

  if (hasInlineFrames && atInlineFrame) {
    auto currentFrame =
        inlineStack[inlineStack.size() - stack.InlineHeight() - 1];
    auto returnAddress = currentFrame.HighPc().ToVirtAddr();
    return RunUntilAddress(returnAddress);
  }
  auto framePointer =
      process->GetRegisters().ReadByIdAs<std::uint64_t>(RegisterId::rbp);
  auto returnAddress =
      process->ReadMemoryAs<std::uint64_t>(VirtAddr{framePointer + 8});
  return RunUntilAddress(VirtAddr{returnAddress});
}

StopReason Target::StepOver() {
  auto origLine = LineEntryAtPc();
  Disassembler disas{*process};
  StopReason reason;
  auto& stack = GetStack();
  do {
    auto inlineStack = stack.InlineStackAtPc();
    auto atStartOfInlineFrame = stack.InlineHeight() > 0;
    if (atStartOfInlineFrame) {
      auto framToSkip = inlineStack[inlineStack.size() - stack.InlineHeight()];
      auto returnAddress = framToSkip.HighPc().ToVirtAddr();
      reason = RunUntilAddress(returnAddress);
      if (!reason.IsStep() || process->GetPc() != returnAddress) {
        return reason;
      }
    } else if (auto instructions = disas.Disassemble(2, process->GetPc());
               instructions[0].text.rfind("call") == 0) {
      reason = RunUntilAddress(instructions[1].address);
      if (!reason.IsStep() || process->GetPc() != instructions[1].address) {
        return reason;
      }
    } else {
      reason = process->StepInstruction();
      if (!reason.IsStep()) return reason;
    }
  } while (LineEntryAtPc() == origLine || LineEntryAtPc()->endSequence);
  return reason;
}

Target::FindFunctionResult Target::FindFunctions(std::string_view name) const {
  FindFunctionResult ret;
  auto dwarfFound = elf->GetDwarf().FindFunctions(std::string{name});
  if (dwarfFound.empty()) {
    auto elfFound = elf->GetSymbolByName(name);
    for (auto sym : elfFound) {
      ret.elfFunctions.emplace_back(elf.get(), sym);
    }
  } else {
    ret.dwarfFunctions.insert(ret.dwarfFunctions.end(), dwarfFound.begin(),
                              dwarfFound.end());
  }
  return ret;
}

#include <cxxabi.h>
std::string Target::FunctionNameAtAddress(VirtAddr address) const {
  auto file_address = address.ToFileAddr(*elf);
  auto obj = file_address.ElfFile();
  if (!obj) return {};

  auto func = obj->GetDwarf().FunctionContainingAddress(file_address);
  if (func && func->Name()) {
    return std::string{*func->Name()};
  } else if (auto elf_func = obj->GetSymbolContainingAddress(file_address);
             elf_func &&
             ELF64_ST_TYPE(elf_func.value()->st_value) == STT_FUNC) {
    auto elf_name = std::string{obj->GetString(elf_func.value()->st_name)};
    return abi::__cxa_demangle(elf_name.c_str(), nullptr, nullptr, nullptr);
  }
  return {};
}

Breakpoint& Target::CreateAdressBreakpoint(VirtAddr address, bool hardware,
                                           bool internal) {
  return breakpoints.Push(std::unique_ptr<AddressBreakpoint>(
      new AddressBreakpoint(*this, address, hardware, internal)));
}

Breakpoint& Target::CreateFunctionBreakpoint(std::string functionName,
                                             bool hardware, bool internal) {
  return breakpoints.Push(std::unique_ptr<FunctionBreakpoint>(
      new FunctionBreakpoint(*this, functionName, hardware, internal)));
}
Breakpoint& Target::CreateLineBreakpoint(std::filesystem::path file,
                                         std::size_t line, bool hardware,
                                         bool internal) {
  return breakpoints.Push(std::unique_ptr<LineBreakpoint>(
      new LineBreakpoint(*this, file, line, hardware, internal)));
}
}  // namespace ldb