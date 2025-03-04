#include <Zycore/Status.h>
#include <Zydis/Disassembler.h>
#include <Zydis/Zydis.h>

#include <libldb/disassembler.hpp>

std::vector<ldb::Disassembler::Instruction> ldb::Disassembler::Disassemble(
    std::size_t n_instructions, std::optional<VirtAddr> address) {
  std::vector<Instruction> ret;
  ret.reserve(n_instructions);
  // If no address is provided, use the current instruction pointer.
  if (!address) {
    address.emplace(process_->GetPc());
  }

  // The largest instruction is 15 bytes.
  auto code = process_->ReadMemoryWithoutTraps(*address, n_instructions * 15);

  ZyanUSize offset = 0;
  ZydisDisassembledInstruction instr;
  while (ZYAN_SUCCESS(ZydisDisassembleATT(ZYDIS_MACHINE_MODE_LONG_64,
                                          address->addr(), code.data() + offset,
                                          code.size() - offset, &instr)) &&
         n_instructions > 0) {
    ret.push_back({.address = *address, .text = std::string{instr.text}});
    offset += instr.info.length;
    *address += instr.info.length;
    n_instructions--;
  }
  return ret;
}