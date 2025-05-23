#pragma once

#include <libldb/dwarf.hpp>

namespace ldb
{
    class target;
    class stack
    {
    public:
        explicit stack(target* tgt)
            : target_{tgt}
        {
        }

        void retset_inline_height();

        /// @brief 获取当前PC处的内联函数栈，从最内层直到最外层的非内联函数
        /// @return 内联函数栈
        std::vector<die> inline_stack_at_pc() const;

        std::uint32_t inline_height() const
        {
            return inline_height_;
        }

        const target& get_target() const
        {
            return *target_;
        }

        void simulate_inline_step_in()
        {
            --inline_height_;
        }

    private:
        target* target_ = nullptr;
        // 0：显示最深的内联函数
        // 1：显示倒数第二深的函数
        // n：显示从最深处向上第 n 层的函数
        std::uint32_t inline_height_ = 0;
    };
} // namespace ldb
