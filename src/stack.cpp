#include <libldb/stack.hpp>
#include <libldb/target.hpp>

std::vector<ldb::die> ldb::stack::inline_stack_at_pc() const
{
    auto pc = target_->get_pc_file_address();
    if (!pc.elf_file())
    {
        return {};
    }
    return pc.elf_file()->get_dwarf().inline_stack_at_address(pc);
}

void ldb::stack::retset_inline_height()
{
    /*
    void a() { std::puts("Hello"); }  // 被内联
    void b() { a(); }                  // 被内联
    void c() { b(); }                  // 被内联
    int main() { c(); }                // 未内联
    */
    // 假设编译后，所有内联函数的第一条指令都在地址 0x1000 处：
    // 场景 1：程序停在 0x1000
    //     当程序停在 0x1000 时：
    //     inline_stack_at_pc() 返回：[main, c, b, a]
    //     reset_inline_height() 从后向前遍历：
    //     a 的 low_pc = 0x1000 ✓ → inline_height_ = 1
    //     b 的 low_pc = 0x1000 ✓ → inline_height_ = 2
    //     c 的 low_pc = 0x1000 ✓ → inline_height_ = 3
    //     main 的 low_pc ≠ 0x1000 ✗ → 停止
    //     最终 inline_height_ = 3
    //     这意味着调试器应该向用户显示 main 函数（从最深处向上 3 层）。
    // 场景 2：单步后停在 0x1004
    //     如果用户执行了单步，现在停在 0x1004（a 函数内部但不是开头）：
    //     inline_stack_at_pc() 返回：[main, c, b, a]
    //     reset_inline_height() 从后向前遍历：
    //     a 的 low_pc = 0x1000，但 PC = 0x1004 ✗ → 停止
    //     最终 inline_height_ = 0
    //     这意味着调试器应该显示最深的内联函数 a。
    auto stack = inline_stack_at_pc();
    inline_height_ = 0;
    auto pc = target_->get_pc_file_address();
    // 从最深的内联函数开始检查，计算有多少个函数的起始地址与当前 PC 相同，这个数量就是需要"向上"的层数。
    for (auto it = stack.rbegin(); it != stack.rend() && it->low_pc() == pc; ++it)
    {
        ++inline_height_;
    }
}
