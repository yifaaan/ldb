#include <libldb/breakpoint.hpp>
#include <libldb/target.hpp>

namespace {
auto GetNextId() {
  static ldb::Breakpoint::IdType id = 0;
  return ++id;
}
}  // namespace

namespace ldb {
Breakpoint::Breakpoint(Target& _target, bool _isHardware, bool _isInternal)
    : target(&_target), isHardware(_isHardware), isInternal(_isInternal) {
  id = isInternal ? -1 : GetNextId();
}

void Breakpoint::Enable() {
  isEnabled = true;
  breakpointSites.ForEach([](auto& site) { site.Enable(); });
}

void Breakpoint::Disable() {
  isEnabled = false;
  breakpointSites.ForEach([](auto& site) { site.Disable(); });
}

void AddressBreakpoint::Resolve() {
  if (breakpointSites.Empty()) {
    auto& newSite = target->GetProcess().CreateBreakpointSite(
        this, nextSiteId++, address, isHardware, isInternal);
    breakpointSites.Push(&newSite);
    if (isEnabled) newSite.Enable();
  }
}

void FunctionBreakpoint::Resolve() {
  auto foundFunctions = target->FindFunctions(functionName);
  // DIE functions
  for (auto& die : foundFunctions.dwarfFunctions) {
    if (die.Contains(DW_AT_low_pc) || die.Contains(DW_AT_ranges)) {
      FileAddr addr;
      // inline functions
      if (die.AbbrevEntry()->tag == DW_TAG_inlined_subroutine) {
        addr = die.LowPc();
      } else {
        auto functionLine = die.Cu()->Lines().GetEntryByAddress(die.LowPc());
        // skip the prologue
        ++functionLine;
        addr = functionLine->address;
      }
      auto loadAddress = addr.ToVirtAddr();
      if (!breakpointSites.ContainsAddress(loadAddress)) {
        auto& newSite = target->GetProcess().CreateBreakpointSite(
            this, nextSiteId++, loadAddress, isHardware, isInternal);
        breakpointSites.Push(&newSite);
        if (isEnabled) newSite.Enable();
      }
    }
  }
  // ELF functions
  for (auto [elf, sym] : foundFunctions.elfFunctions) {
    auto fileAddress = FileAddr{*elf, sym->st_value};
    auto loadAddress = fileAddress.ToVirtAddr();
    if (!breakpointSites.ContainsAddress(loadAddress)) {
      auto& newSite = target->GetProcess().CreateBreakpointSite(
          this, nextSiteId++, loadAddress, isHardware, isInternal);
      breakpointSites.Push(&newSite);
      if (isEnabled) newSite.Enable();
    }
  }
}

void LineBreakpoint::Resolve() {
  auto& dwarf = target->GetElf().GetDwarf();
  for (auto& cu : dwarf.CompileUnits()) {
    auto entries = cu->Lines().GetEntriesByLine(file, line);
    for (auto entry : entries) {
      auto& dwarf = entry->address.ElfFile()->GetDwarf();
      auto stack = dwarf.InlineStackAtAddress(entry->address);
      auto noInlineStack = stack.size() == 1;
      auto shouldSkipPrologue = noInlineStack &&
                                (stack[0].Contains(DW_AT_ranges) ||
                                 stack[0].Contains(DW_AT_low_pc)) &&
                                stack[0].LowPc() == entry->address;
      if (shouldSkipPrologue) ++entry;
      auto loadAddress = entry->address.ToVirtAddr();
      if (!breakpointSites.ContainsAddress(loadAddress)) {
        auto& newSite = target->GetProcess().CreateBreakpointSite(
            this, nextSiteId++, loadAddress, isHardware, isInternal);
        breakpointSites.Push(&newSite);
        if (isEnabled) newSite.Enable();
      }
    }
  }
}
}  // namespace ldb