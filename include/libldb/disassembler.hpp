#pragma once

#include <libldb/process.hpp>
#include <string>

namespace ldb
{
    /// @brief 反汇编器
    class disassembler
    {
        /// @brief 反汇编的指令
        struct instruction
        {
            virt_addr address;
            std::string text;
        };

    public:
        disassembler(process& proc)
            : process_(&proc)
        {
        }

        /// @brief 反汇编
        /// @param n_instructions 反汇编的指令数
        /// @param address 反汇编的地址
        /// @return 反汇编的指令
        std::vector<instruction> disassemble(std::size_t n_instructions, std::optional<virt_addr> address = std::nullopt);

    private:
        process* process_;
    };
} // namespace ldb
