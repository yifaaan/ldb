#ifndef LDB_DISASSEMBLER_HPP
#define LDB_DISASSEMBLER_HPP

#include <libldb/process.hpp>
#include <optional>

namespace ldb
{

    class disassember
    {
    private:
        struct instruction
        {
            virt_addr address;
            std::string text;
        };

    public:
        disassember(process& proc)
                : process_(&proc)
        {}

        std::vector<instruction> disassemble(std::size_t n_instructions, std::optional<virt_addr> address = std::nullopt); 

    private:
        process* process_;
    };
}

#endif