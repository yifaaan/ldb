#include <Zydis/Zydis.h>

#include <libldb/disassembler.hpp>

namespace ldb
{
	std::vector<Disassembler::Instruction> Disassembler::Disassemble(std::size_t nInstructions, std::optional<VirtAddr> address)
	{
		std::vector<Instruction> ret;
		ret.reserve(nInstructions);
		if (!address)
		{
			address.emplace(process->GetPc());
		}
		//  the largest x64 instruction is 15 bytes
		auto code = process->ReadMemoryWithoutTraps(*address, nInstructions * 15);
		ZyanUSize offset = 0;
		ZydisDisassembledInstruction instr;
		while (ZYAN_SUCCESS(ZydisDisassembleATT(ZYDIS_MACHINE_MODE_LONG_64, address->Addr(), code.data() + offset, code.size() - offset, &instr)) && nInstructions > 0)
		{
			ret.emplace_back(*address, std::string(instr.text));
			offset += instr.info.length;
			*address += instr.info.length;
			--nInstructions;
		}
		return ret;
	}
}