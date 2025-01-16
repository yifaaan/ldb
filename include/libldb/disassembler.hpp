#pragma once

#include <optional>

#include <libldb/process.hpp>

namespace ldb
{
    class Disassembler
    {
        struct Instruction
        {
            VirtAddr address;
            std::string text;
        };

    public:
        explicit Disassembler(Process& proc) : process(&proc) {}

        /// default address is pc
        std::vector<Instruction> Disassemble(std::size_t nInstructions, std::optional<VirtAddr> address = std::nullopt);


    private:
        Process* process;
    };
}