#include <Zydis/Zydis.h>

#include <libldb/disassembler.hpp>

std::vector<ldb::disassembler::instruction> ldb::disassembler::disassemble(std::size_t n_instructions, std::optional<virt_addr> address)
{
    std::vector<instruction> ret;
    ret.reserve(n_instructions);
    if (!address)
    {
        address.emplace(process_->get_pc());
    }
    auto code = process_->read_memory_without_traps(*address, n_instructions * 15);
    ZyanUSize offset = 0;
    ZydisDisassembledInstruction instr;
    while (ZYAN_SUCCESS(ZydisDisassembleATT(ZYDIS_MACHINE_MODE_LONG_64, address->addr(), code.data() + offset, code.size() - offset, &instr)) &&
           n_instructions > 0)
    {
        ret.emplace_back(*address, std::string{instr.text});
        *address += instr.info.length;
        offset += instr.info.length;
        n_instructions--;
    }
    return ret;
}
