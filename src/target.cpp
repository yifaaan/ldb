#include <csignal>
#include <libldb/disassembler.hpp>
#include <libldb/elf.hpp>
#include <libldb/target.hpp>
#include <libldb/types.hpp>

namespace
{
    std::unique_ptr<ldb::elf> create_loaded_elf(const ldb::process& process, const std::filesystem::path& path)
    {
        auto auxv = process.get_auxv();
        auto obj = std::make_unique<ldb::elf>(path);

        std::uint64_t actual_entry_point = auxv.at(AT_ENTRY);
        ldb::virt_addr load_bias{actual_entry_point - obj->header().e_entry};
        // 设置加载偏移量
        obj->notify_loaded(load_bias);
        return obj;
    }
} // namespace

std::unique_ptr<ldb::target> ldb::target::launch(std::filesystem::path path, std::optional<int> stdout_replacement)
{
    auto proc = process::launch(path, true, stdout_replacement);
    if (!proc)
    {
        return nullptr;
    }
    auto obj = create_loaded_elf(*proc, path);
    if (!obj)
    {
        return nullptr;
    }
    auto tgt = std::unique_ptr<target>(new target(std::move(proc), std::move(obj)));
    tgt->get_process().set_target(tgt.get());
    return tgt;
}

std::unique_ptr<ldb::target> ldb::target::attach(pid_t pid)
{
    // 获取进程的 ELF 路径
    auto elf_path = std::filesystem::path{"/proc"} / std::to_string(pid) / "exe";
    auto proc = process::attach(pid);
    if (!proc)
    {
        return nullptr;
    }
    auto obj = create_loaded_elf(*proc, elf_path);
    if (!obj)
    {
        return nullptr;
    }
    auto tgt = std::unique_ptr<target>(new target(std::move(proc), std::move(obj)));
    tgt->get_process().set_target(tgt.get());
    return tgt;
}

ldb::file_addr ldb::target::get_pc_file_address() const
{
    return process_->get_pc().to_file_addr(*elf_);
}

void ldb::target::notify_stop(const ldb::stop_reason& reason)
{
    stack_.retset_inline_height();
}

ldb::stop_reason ldb::target::step_in()
{
    auto& stack = get_stack();
    // inline_height()>0 说明调试器此刻伪装在“调用者”帧；实际 PC 仍停在被内联函数首指令
    if (stack.inline_height() > 0)
    {
        // 模拟内联函数栈的单步执行
        // 源码 main()->foo()->bar() 全部被内联至一条指令 A。
        // inline_height_=2 时显示在 main。
        // 执行 step_in 后 inline_height_ 变 1，调试器显示进入 foo，但 PC 仍是 A。
        stack.simulate_inline_step_in();
        return stop_reason{process_state::stopped, SIGTRAP, trap_type::single_step};
    }

    // 假设某函数对应的 DWARF 行表大致如下（地址 → 行号 / 标记）：
    // | 地址 | 行号 | 备注 |
    // |------|------|---------------------|
    // |0x1000| 42 | orig_line 起点 |
    // |0x1002| 42 | 同一源行的下一条指令|
    // |0x1004| 42 | 同一源行的下一条指令|
    // |0x1006| end | end_sequence 标记 |
    // |0x1008| 43 | 下一条真正的源行 |
    // 调试器开始时 PC = 0x1000，对应 orig_line＝行 42。

    // 说明在最内层的函数中
    auto orig_line = line_entry_at_pc();
    // 单步执行直到新的源码行
    do
    {
        auto reason = process_->step_instruction();
        if (!reason.is_step())
        {
            // 可能命中断点、进程退出等，直接返回
            return reason;
        }
    }
    while ((line_entry_at_pc() == orig_line || line_entry_at_pc()->end_sequence) && line_entry_at_pc() != line_table::iterator{});

    auto pc = get_pc_file_address();
    if (pc.elf_file())
    {
        auto& dwarf = pc.elf_file()->get_dwarf();
        auto func = dwarf.function_containing_address(pc);
        // 如果当前PC在函数范围内，则跳过函数序言
        if (func && func->low_pc() == pc)
        {
            // 序言对应的条目
            auto line = line_entry_at_pc();
            if (line != line_table::iterator{})
            {
                ++line;
                // 跳过序言后，停到函数体第一行
                return run_until_address(line->address.to_virt_addr());
            }
        }
    }
    return stop_reason{process_state::stopped, SIGTRAP, trap_type::single_step};
}

ldb::line_table::iterator ldb::target::line_entry_at_pc() const
{
    auto pc = get_pc_file_address();
    if (!pc.elf_file())
    {
        return {};
    }
    auto cu = pc.elf_file()->get_dwarf().compile_unit_containing_address(pc);
    if (!cu)
    {
        return {};
    }
    return cu->lines().get_entry_by_address(pc);
}

ldb::stop_reason ldb::target::run_until_address(virt_addr address)
{
    breakpoint_site* breakpoint_to_remove = nullptr;
    // 该地址没有断点，创建内部临时断点
    if (!process_->breakpoint_sites().contains_address(address))
    {
        breakpoint_to_remove = &process_->create_breakpoint_site(address, false, true);
        breakpoint_to_remove->enable();
    }
    process_->resume();
    auto reason = process_->wait_on_signal();
    if (reason.is_breakpoint() && process_->get_pc() == address)
    {
        // 对用户来说表现为单步到达
        reason.trap_reason = trap_type::single_step;
    }
    if (breakpoint_to_remove)
    {
        process_->breakpoint_sites().remove_by_address(breakpoint_to_remove->address());
    }
    return reason;
}

ldb::stop_reason ldb::target::step_over()
{
    auto orig_line = line_entry_at_pc();
    //
    disassembler disas{*process_};
    ldb::stop_reason reason;
    auto& stack = get_stack();

    do
    {
        auto inline_stack = stack.inline_stack_at_pc();
        // | inline_stack_at_pc() | 计算当前 PC 处的内联栈 | [foo, bar] 表示 bar 被内联进 foo |
        // | at_start_of_inline | 若 inline_height()>0，说明调试器声称停在调用者，但 PC 仍在被内联函数首指令 | 用户看到的行号在 foo()，实际 PC 是 bar() 入口 |
        bool at_start_of_inline = !inline_stack.empty();
        if (at_start_of_inline)
        {
            auto frame_to_skip = inline_stack[inline_stack.size() - stack.inline_height()];
            // 越过整个内联函数
            auto ret_addr = frame_to_skip.high_pc().to_virt_addr();
            reason = run_until_address(ret_addr);
            // 被内联函数中设置的断点打断，直接返回
            if (!reason.is_step() || process_->get_pc() != ret_addr)
            {
                return reason;
            }
        }
        else if (auto ins = disas.disassemble(2, process_->get_pc()); ins[0].text.rfind("call") == 0)
        {
            // 若第 1 条文本以 "call" 开头，说明是函数调用；第 2 条指令地址就是返回后要执行的下一条
            // 越过call指令
            reason = run_until_address(ins[1].address);
            if (!reason.is_step() || process_->get_pc() != ins[1].address)
            {
                return reason;
            }
        }
        else
        {
            reason = process_->step_instruction();
            if (!reason.is_step())
            {
                return reason;
            }
        }
    } // 仍在同一个源码行，就继续
    while ((line_entry_at_pc() == orig_line || line_entry_at_pc()->end_sequence) && line_entry_at_pc() != line_table::iterator{});
    return reason;
}

ldb::stop_reason ldb::target::step_out()
{
    // 普通函数（非内联）
    // 假设当前函数 A 调用了 B，CPU 停在 B 内部某条指令上，此时寄存器里：
    // rbp = 0x7fffffffe550
    // 在内存 0x7fffffffe558 处存着返回到 A 的地址 0x400123
    // 执行 “step out” 时：
    // 1）读取 frame_pointer = 0x7fffffffe550
    // 2）读取 return_address = M[0x7fffffffe558] = 0x400123
    // 3）在 0x400123 处设置断点并继续运行，当 B 返回到 A 时命中该断点
    // 内联函数
    // 假设函数 A 中内联了函数 X，代码在编译单元里并未生成真实调用栈帧，DWARF 信息把 X 的范围标记为 [0x400200, 0x400210)。如果 CPU 停在 X 的某条内联指令上：
    // inline_stack_at_pc() 会返回两帧：最外层 A（tag=DW_TAG_subprogram），内层 X（tag=DW_TAG_inlined_subroutine）
    // inline_height() 表示当前处于内联深度 1，未到最外层
    // 执行 “step out” 时会走内联分支：
    // 1）取出当前内联帧 X 的 high_pc()，即虚拟地址 0x400210
    // 2）run_until_address(0x400210)，继续运行直到执行完 X 的最后一条内联指令，回到 A 的上下文
    // 这样，无论是在普通调用还是内联调用中，都能合理地“跳出”当前函数或内联块。
    auto& stack = get_stack();
    auto inline_stack = stack.inline_stack_at_pc();
    auto has_inline_frames = inline_stack.size() > 1;
    // 是否在inline函数中
    auto at_inline_frame = stack.inline_height() < inline_stack.size() - 1;
    if (has_inline_frames && at_inline_frame)
    {
        // 当前正在执行的内联函数
        auto current_frame = inline_stack[inline_stack.size() - stack.inline_height() - 1];
        auto ret_addr = current_frame.high_pc().to_virt_addr();
        return run_until_address(ret_addr);
    }
    // 读rbp
    auto frame_pointer = process_->get_registers().read_by_id_as<std::uint64_t>(register_id::rbp);
    // 读取返回地址：rbp+8处
    auto ret_addr = process_->read_memory_as<std::uint64_t>(virt_addr{frame_pointer + 8});
    return run_until_address(virt_addr{ret_addr});
}
