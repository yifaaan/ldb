#include <iostream>

#include <Zydis/Zydis.h>

#include <libldb/disassembler.hpp>

std::vector<ldb::Disassembler::Instruction> ldb::Disassembler::Disassemble(std::size_t nInstructions, std::optional<VirtAddr> address)
{
    std::vector<Instruction> ret;
    ret.reserve(nInstructions);
    if (!address)
    {
        address.emplace(process->GetPc());
    }
    // the largest x64 instruction is 15 bytes
    auto code = process->ReadMemoryWithoutTraps(*address, nInstructions * 15);

    ZyanUSize offset = 0;
    ZydisDisassembledInstruction instr;
    while (
        ZYAN_SUCCESS(
            ZydisDisassembleATT(
                ZYDIS_MACHINE_MODE_LONG_64, address->Addr(),
                code.data() + offset,
                code.size() - offset, &instr)) 
                and nInstructions > 0)
    {
        ret.push_back(Instruction{ *address, std::string(instr.text) });
        offset += instr.info.length;
        *address += instr.info.length;
        --nInstructions;
    }
   return ret;
}