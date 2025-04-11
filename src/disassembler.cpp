#include <Zydis/Zydis.h>

#include <libldb/disassembler.hpp>

namespace ldb
{
	std::vector<Disassembler::Instruction> Disassembler::Disassemble(std::size_t nInstructions, std::optional<VirtAddr> address)
	{
		std::vector<Instruction> ret(nInstructions);
		if (!address)
		{
			address.emplace(process->GetPc());
		}
		//  the largest x64 instruction is 15 bytes
		auto code = process->ReadMemory(*address, nInstructions * 15);
		ZyanUSize offset = 0;
		ZydisDisassembledInstruction instr;
	}
}