#include <elf.h>
#include <fmt/base.h>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <readline/history.h>
#include <readline/readline.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <libldb/disassembler.hpp>
#include <libldb/error.hpp>
#include <libldb/libldb.hpp>
#include <libldb/parse.hpp>
#include <libldb/process.hpp>
#include <libldb/syscalls.hpp>
#include <ranges>
#include <string_view>
#include <type_traits>
#include <vector>

namespace
{
    ldb::process* g_ldb_process = nullptr;

    void handle_sigint(int)
    {
        if (g_ldb_process)
        {
            kill(g_ldb_process->pid(), SIGSTOP);
        }
    }
    std::vector<std::string_view> split(std::string_view str, std::string_view delimiter)
    {
        std::vector<std::string_view> result;
        for (const auto& subrange : std::ranges::split_view{str, delimiter})
        {
            result.emplace_back(subrange.begin(), subrange.end());
        }
        return result;
    }

    bool is_prefix(std::string_view str, std::string_view of)
    {
        return of.starts_with(str);
    }

    ldb::registers::value parse_register_value(const ldb::register_info& info, std::string_view text)
    {
        try
        {
            if (info.format == ldb::register_format::uint)
            {
                switch (info.size)
                {
                case 1:
                    return ldb::to_integral<std::uint8_t>(text, 16).value();
                case 2:
                    return ldb::to_integral<std::uint16_t>(text, 16).value();
                case 4:
                    return ldb::to_integral<std::uint32_t>(text, 16).value();
                case 8:
                    return ldb::to_integral<std::uint64_t>(text, 16).value();
                }
            }
            else if (info.format == ldb::register_format::double_float)
            {
                return ldb::to_float<double>(text).value();
            }
            else if (info.format == ldb::register_format::long_double)
            {
                return ldb::to_float<long double>(text).value();
            }
            else if (info.format == ldb::register_format::vector)
            {
                if (info.size == 8)
                {
                    return ldb::parse_vector<8>(text);
                }
                else if (info.size == 16)
                {
                    return ldb::parse_vector<16>(text);
                }
            }
        }
        catch (...)
        {
        }
        ldb::error::send("Invalid format");
    }

    std::string get_sigtrap_info(const ldb::process& proc, ldb::stop_reason reason)
    {
        if (!reason.trap_reason)
        {
            return " (unknown SIGTRAP reason)";
        }
        if (reason.trap_reason == ldb::trap_type::software_break)
        {
            try
            {
                // 软件断点在wait_on_signal里面已经恢复了pc，所以这里可以获取到
                auto& site = proc.breakpoint_sites().get_by_address(proc.get_pc());
                return fmt::format(" (breakpoint id {})", site.id());
            }
            catch (const std::out_of_range& err)
            {
                return " (breakpoint not found)";
            }
        }
        else if (reason.trap_reason == ldb::trap_type::hardware_break)
        {
            auto id_variant = proc.get_current_hardware_stoppoint();
            if (id_variant.index() == 0)
            {
                return fmt::format(" (hardwarebreakpoint id {})", std::get<0>(id_variant));
            }
            else
            {
                std::string message;
                auto& watchpoint = proc.watchpoints().get_by_id(std::get<1>(id_variant));
                message += fmt::format(" (watchpoint id {})", watchpoint.id());
                if (watchpoint.data() == watchpoint.previous_data())
                {
                    message += fmt::format("\nValue: {:#x}", watchpoint.data());
                }
                else
                {
                    message += fmt::format("\nOld value: {:#x}", watchpoint.previous_data());
                    message += fmt::format("\nNew value: {:#x}", watchpoint.data());
                }
                return message;
            }
        }
        else if (reason.trap_reason == ldb::trap_type::single_step)
        {
            return " (single step)";
        }
        else if (reason.trap_reason == ldb::trap_type::syscall)
        {
            const auto& info = *reason.syscall_info;
            std::string message = " ";
            if (info.entry)
            {
                message += "(syscall entry)\n";
                message += fmt::format("syscall : {}({:#x})", ldb::syscall_id_to_name(info.id), fmt::join(info.args, ", "));
            }
            else
            {
                message += "(syscall exit)\n";
                message += fmt::format("syscall {} returned: {:#x} ({})", ldb::syscall_id_to_name(info.id), info.ret, info.ret);
            }
            return message;
        }
        return "";
    }

    /// @brief 打印进程停止原因
    /// @param proc 进程
    /// @param reason 停止原因
    void print_stop_reason(const ldb::process& proc, ldb::stop_reason reason)
    {
        std::string message;
        // waitpid之后，进程所处状态
        switch (reason.reason)
        {
        case ldb::process_state::exited:
            message = fmt::format("Exited with status {}", static_cast<int>(reason.info));
            break;
        case ldb::process_state::terminated:
            message = fmt::format("Terminated with signal {}", sigabbrev_np(reason.info));
            break;
        case ldb::process_state::stopped:
            message = fmt::format("Stopped with signal {} at {:#x}", sigabbrev_np(reason.info), proc.get_pc().addr());
            if (reason.info == SIGTRAP)
            {
                // 获取更详细的SIGTRAP信息
                message += get_sigtrap_info(proc, reason);
            }
            break;
        default:
            std::cout << "Unknown stop reason";
            break;
        }
        fmt::print("Process {} {}\n", proc.pid(), message);
    }

    /// @brief 打印帮助信息
    /// @param args 命令参数
    void print_help(const std::vector<std::string_view>& args)
    {
        if (args.size() == 1)
        {
            std::cerr << R"(Available commands:
            continue    - Resume the process
            register    - Commands for operating on registers
            breakpoint  - Commands for operating on breakpoints
            step        - Single step the process
            memory      - Commands for operating on memory
            disassemble - Disassemble machine code to assembly
            watchpoint  - Commands for operating on watchpoints
            catchpoint  - Commands for operating on catchpoints
            )";
        }
        else if (is_prefix(args[1], "register"))
        {
            std::cerr << R"(Available commands:
            read
            read <register>
            read all
            write <register> <value>
            )";
        }
        else if (is_prefix(args[1], "breakpoint"))
        {
            std::cerr << R"(Available commands:
            list
            set <address>
            set <address> -h
            enable <id>
            disable <id>
            delete <id>
            )";
        }
        else if (is_prefix(args[1], "memory"))
        {
            std::cerr << R"(Available commands:
            read <address>
            read <address> <n_bytes>
            write <address> <data>
            )";
        }
        else if (is_prefix(args[1], "disassemble"))
        {
            std::cerr << R"(Available commands:
            -a <address>
            -c <n_instructions>
            )";
        }
        else if (is_prefix(args[1], "watchpoint"))
        {
            std::cerr << R"(Available commands:
            list
            delete <id>
            enable <id>
            disable <id>
            set <address> <write|rw|execute> <size>
            )";
        }
        else if (is_prefix(args[1], "catchpoint"))
        {
            std::cerr << R"(Available commands:
            syscall
            syscall none
            syscall <list of syscall ids or names>
            )";
        }
        else
        {
            std::cerr << "No help available for this command\n";
        }
    }

    /// @brief 打印反汇编
    /// @param proc 进程
    /// @param address 地址
    /// @param n_instructions 反汇编的指令数
    void print_disassembly(ldb::process& proc, ldb::virt_addr address, std::size_t n_instructions)
    {
        auto disassembler = ldb::disassembler{proc};
        auto instructions = disassembler.disassemble(n_instructions, address);
        std::ranges::for_each(instructions,
                              [](const auto& instr)
                              {
                                  fmt::print("{:#018x}: {}\n", instr.address.addr(), instr.text);
                              });
    }

    /// @brief 处理进程停止
    /// @param proc 进程
    /// @param reason 停止原因
    void handle_stop(ldb::process& proc, ldb::stop_reason reason)
    {
        print_stop_reason(proc, reason);
        if (reason.reason == ldb::process_state::stopped)
        {
            print_disassembly(proc, proc.get_pc(), 5);
        }
    }

    /// @brief 读取寄存器
    /// @param proc 进程
    /// @param args 命令参数
    void handle_register_read(ldb::process& proc, std::span<std::string_view> args)
    {
        auto format = [](auto v)
        {
            if constexpr (std::is_floating_point_v<decltype(v)>)
            {
                return fmt::format("{}", v);
            }
            else if constexpr (std::is_integral_v<decltype(v)>)
            {
                return fmt::format("{:#0{}x}", v, sizeof(v) * 2 + 2);
            }
            else
            {
                return fmt::format("[{:#04x}]", fmt::join(v, ","));
            }
        };
        if (args.size() == 2 || args.size() == 3 && args[2] == "all")
        {
            for (const auto& info : ldb::g_register_infos)
            {
                // 如果命令参数是3个(read all)，或者寄存器类型是通用寄存器，并且寄存器名不是orig_rax
                auto should_print = (args.size() == 3 || info.type == ldb::register_type::gpr) && info.name != "orig_rax";
                if (!should_print)
                {
                    continue;
                }
                auto value = proc.get_registers().read(info);
                fmt::print("{}:\t{}\n", info.name, std::visit(format, value));
            }
        }
        else if (args.size() == 3)
        {
            // 如果命令参数是3个，并且寄存器类型是通用寄存器
            try
            {
                auto info = ldb::register_info_by_name(args[2]);
                auto value = proc.get_registers().read(info);
                fmt::print("{}:\t{}\n", info.name, std::visit(format, value));
            }
            catch (const ldb::error& err)
            {
                std::cerr << "No such register: " << args[2] << std::endl;
                return;
            }
        }
        else
        {
            print_help({"help", "register"});
        }
    }

    void handle_register_write(ldb::process& proc, std::span<std::string_view> args)
    {
        if (args.size() != 4)
        {
            print_help({"help", "register"});
            return;
        }
        try
        {
            auto info = ldb::register_info_by_name(args[2]);
            auto value = parse_register_value(info, args[3]);
            proc.get_registers().write(info, value);
        }
        catch (const ldb::error& err)
        {
            std::cerr << err.what() << std::endl;
            return;
        }
    }

    void handle_register_command(ldb::process& proc, std::span<std::string_view> args)
    {
        if (args.size() < 2)
        {
            print_help({"help", "register"});
            return;
        }
        if (is_prefix(args[1], "read"))
        {
            handle_register_read(proc, args);
        }
        else if (is_prefix(args[1], "write"))
        {
            handle_register_write(proc, args);
        }
        else
        {
            print_help({"help", "register"});
        }
    }
    void handle_breakpoint_command(ldb::process& proc, std::span<std::string_view> args)
    {
        if (args.size() < 2)
        {
            print_help({"help", "breakpoint"});
            return;
        }

        auto command = args[1];
        if (is_prefix(command, "list"))
        {
            if (proc.breakpoint_sites().empty())
            {
                fmt::print("No breakpoints set\n");
            }
            else
            {
                fmt::print("Current breakpoints:\n");
                proc.breakpoint_sites().for_each(
                [](const auto& site)
                {
                    if (site.is_internal())
                        return;
                    fmt::print("{}: address = {:#x}, {}\n", site.id(), site.address().addr(), site.is_enabled() ? "enabled" : "disabled");
                });
            }
            return;
        }

        if (args.size() < 3)
        {
            print_help({"help", "breakpoint"});
        }
        if (is_prefix(command, "set"))
        {
            auto address = ldb::to_integral<std::uint64_t>(args[2], 16);
            if (!address)
            {
                fmt::print(stderr, "Breakpoint command expects address in hexadecimal, prefixed with 0x\n");
                return;
            }
            // 是否是硬件断点
            // Example: breakpoint set 0x12345678 -h
            bool is_hardware = false;
            if (args.size() == 4)
            {
                if (args[3] == "-h")
                {
                    is_hardware = true;
                }
                else
                {
                    ldb::error::send("Invalid breakpoint command argument");
                }
            }
            proc.create_breakpoint_site(ldb::virt_addr{*address}, is_hardware, false).enable();
            return;
        }

        auto id = ldb::to_integral<std::uint64_t>(args[2]);
        if (!id)
        {
            fmt::print(stderr, "Breakpoint command expects breakpoint id in decimal\n");
            return;
        }
        if (is_prefix(command, "enable"))
        {
            proc.breakpoint_sites().get_by_id(*id).enable();
        }
        else if (is_prefix(command, "disable"))
        {
            proc.breakpoint_sites().get_by_id(*id).disable();
        }
        else if (is_prefix(command, "delete"))
        {
            proc.breakpoint_sites().remove_by_id(*id);
        }
    }

    void handle_memory_read_command(ldb::process& proc, std::span<std::string_view> args)
    {
        auto address = ldb::to_integral<std::uint64_t>(args[2], 16);
        if (!address)
        {
            ldb::error::send("Invalid address format");
        }
        auto n_bytes = 32;
        if (args.size() == 4)
        {
            auto bytes = ldb::to_integral<std::uint64_t>(args[3]);
            if (!bytes)
            {
                ldb::error::send("Invalid number of bytes format");
            }
            n_bytes = *bytes;
        }
        auto data = proc.read_memory(ldb::virt_addr{*address}, n_bytes);
        for (int i = 0; i < data.size(); i += 16)
        {
            auto start = data.begin() + i;
            auto end = data.begin() + std::min<int>(i + 16, data.size());
            fmt::print("{:#016x}: {:02x}\n", *address + i, fmt::join(start, end, " "));
        }
    }

    void handle_memory_write_command(ldb::process& proc, std::span<std::string_view> args)
    {
        if (args.size() != 4)
        {
            print_help({"help", "memory"});
            return;
        }
        auto address = ldb::to_integral<std::uint64_t>(args[2], 16);
        if (!address)
        {
            ldb::error::send("Invalid address format");
        }
        auto data = ldb::parse_vector(args[3]);
        proc.write_memory(ldb::virt_addr{*address}, data);
    }

    void handle_memory_command(ldb::process& proc, std::span<std::string_view> args)
    {
        if (args.size() < 3)
        {
            print_help({"help", "memory"});
            return;
        }
        if (is_prefix(args[1], "read"))
        {
            handle_memory_read_command(proc, args);
        }
        else if (is_prefix(args[1], "write"))
        {
            handle_memory_write_command(proc, args);
        }
        else
        {
            print_help({"help", "memory"});
        }
    }

    void handle_disassemble_command(ldb::process& proc, std::span<std::string_view> args)
    {
        auto address = proc.get_pc();
        auto n_instructions = 5;
        auto it = args.begin() + 1;
        while (it != args.end())
        {
            if (*it == "-a" && it + 1 != args.end())
            {
                it++;
                auto address_opt = ldb::to_integral<std::uint64_t>(*it, 16);
                if (!address_opt)
                {
                    ldb::error::send("Invalid address format");
                }
                address = ldb::virt_addr{*address_opt};
            }
            else if (*it == "-c" && it + 1 != args.end())
            {
                it++;
                auto n_instructions_opt = ldb::to_integral<std::size_t>(*it);
                if (!n_instructions_opt)
                {
                    ldb::error::send("Invalid number of instructions format");
                }
                n_instructions = *n_instructions_opt;
            }
            else
            {
                print_help({"help", "disassemble"});
                return;
            }
        }
        print_disassembly(proc, address, n_instructions);
    }

    void handle_watchpoint_list(ldb::process& proc, std::span<std::string_view> args)
    {
        auto stoppoint_mode_to_string = [](ldb::stoppoint_mode mode)
        {
            switch (mode)
            {
            case ldb::stoppoint_mode::read_write:
                return "read_write";
            case ldb::stoppoint_mode::write:
                return "write";
            case ldb::stoppoint_mode::execute:
                return "execute";
            }
            return "unknown mode";
        };
        if (proc.watchpoints().empty())
        {
            fmt::print("No watchpoints set\n");
        }
        else
        {
            fmt::print("Current watchpoints:\n");
            proc.watchpoints().for_each(
            [&](const auto& watchpoint)
            {
                fmt::print("{}: address = {:#x}, mode = {}, size = {}, {}\n",
                           watchpoint.id(),
                           watchpoint.address().addr(),
                           stoppoint_mode_to_string(watchpoint.mode()),
                           watchpoint.size(),
                           watchpoint.is_enabled() ? "enabled" : "disabled");
            });
        }
    }

    void handle_watchpoint_set(ldb::process& proc, std::span<std::string_view> args)
    {
        if (args.size() != 5)
        {
            print_help({"help", "watchpoint"});
            return;
        }
        auto address = ldb::to_integral<std::uint64_t>(args[2], 16);
        auto mode_text = args[3];
        auto size = ldb::to_integral<std::size_t>(args[4]);
        if (!address || !size || !(mode_text == "execute" || mode_text == "write" || mode_text == "rw"))
        {
            print_help({"help", "watchpoint"});
            return;
        }
        ldb::stoppoint_mode mode;
        if (mode_text == "execute")
        {
            mode = ldb::stoppoint_mode::execute;
        }
        else if (mode_text == "write")
        {
            mode = ldb::stoppoint_mode::write;
        }
        else if (mode_text == "rw")
        {
            mode = ldb::stoppoint_mode::read_write;
        }
        proc.create_watchpoint(ldb::virt_addr{*address}, mode, *size).enable();
    }

    void handle_watchpoint_command(ldb::process& proc, std::span<std::string_view> args)
    {
        if (args.size() < 2)
        {
            print_help({"help", "watchpoint"});
        }
        auto command = args[1];
        if (is_prefix(command, "list"))
        {
            handle_watchpoint_list(proc, args);
            return;
        }
        if (is_prefix(command, "set"))
        {
            handle_watchpoint_set(proc, args);
            return;
        }

        if (args.size() < 3)
        {
            print_help({"help", "watchpoint"});
        }
        auto id_opt = ldb::to_integral<std::uint64_t>(args[2]);
        if (!id_opt)
        {
            fmt::print(stderr, "Watchpoint command expects watchpoint id in decimal\n");
            return;
        }
        auto id = *id_opt;
        if (is_prefix(command, "enable"))
        {
            try
            {
                proc.watchpoints().get_by_id(id).enable();
            }
            catch (const ldb::error& err)
            {
                fmt::print(stderr, "No such watchpoint: {}\n", id);
            }
        }
        else if (is_prefix(command, "disable"))
        {
            try
            {
                proc.watchpoints().get_by_id(id).disable();
            }
            catch (const ldb::error& err)
            {
                fmt::print(stderr, "No such watchpoint: {}\n", id);
            }
        }
        else if (is_prefix(command, "delete"))
        {
            try
            {
                proc.watchpoints().remove_by_id(id);
            }
            catch (const ldb::error& err)
            {
                fmt::print(stderr, "No such watchpoint: {}\n", id);
            }
        }
        else
        {
            fmt::print(stderr, "Unknown watchpoint command: {}\n", command);
            print_help({"help", "watchpoint"});
        }
    }

    void handle_syscall_catchpoint_command(ldb::process& proc, std::span<std::string_view> args)
    {
        auto policy = ldb::syscall_catch_policy::catch_all();
        if (args.size() == 3 && args[2] == "none")
        {
            policy = ldb::syscall_catch_policy::catch_none();
        }
        else if (args.size() >= 3)
        {
            auto syscall_ids = split(args[2], ",");
            std::vector<int> to_catch;
            to_catch.reserve(syscall_ids.size());
            std::ranges::transform(syscall_ids,
                                   std::back_inserter(to_catch),
                                   [](auto sv)
                                   {
                                       return std::isdigit(sv[0]) ? ldb::to_integral<int>(sv).value() : ldb::syscall_name_to_id(sv);
                                   });
            policy = ldb::syscall_catch_policy::catch_some(std::move(to_catch));
        }
        proc.set_syscall_catch_policy(std::move(policy));
    }

    void handle_catchpoint_command(ldb::process& proc, std::span<std::string_view> args)
    {
        if (args.size() < 2)
        {
            print_help({"help", "catchpoint"});
            return;
        }
        if (is_prefix(args[1], "syscall"))
        {
            handle_syscall_catchpoint_command(proc, args);
        }
    }

    std::unique_ptr<ldb::process> attach(int argc, const char** argv)
    {
        pid_t pid = 0;
        if (argc == 3 && argv[1] == std::string_view{"-p"})
        {
            pid = std::atoi(argv[2]);
            return ldb::process::attach(pid);
        }
        else
        {
            auto program_path = argv[1];
            auto proc = ldb::process::launch(program_path);
            fmt::print("Launched process with pid {}\n", proc->pid());
            return proc;
        }
    }

    void handle_command(std::unique_ptr<ldb::process>& proc, std::string_view line)
    {
        auto args = split(line, " ");
        auto command = args[0];
        if (is_prefix(command, "help"))
        {
            print_help(args);
        }
        else if (is_prefix(command, "continue"))
        {

            proc->resume();
            auto reason = proc->wait_on_signal();
            handle_stop(*proc, reason);
        }
        else if (is_prefix(command, "register"))
        {
            handle_register_command(*proc, args);
        }
        else if (is_prefix(command, "breakpoint"))
        {
            handle_breakpoint_command(*proc, args);
        }
        else if (is_prefix(command, "step"))
        {
            auto reason = proc->step_instruction();
            handle_stop(*proc, reason);
        }
        else if (is_prefix(command, "memory"))
        {
            handle_memory_command(*proc, args);
        }
        else if (is_prefix(command, "disassemble"))
        {
            handle_disassemble_command(*proc, args);
        }
        else if (is_prefix(command, "watchpoint"))
        {
            handle_watchpoint_command(*proc, args);
        }
        else if (is_prefix(command, "catchpoint"))
        {
            handle_catchpoint_command(*proc, args);
        }
        else
        {
            std::cerr << "Unknown command: " << command << "\n";
        }
    }

    void main_loop(std::unique_ptr<ldb::process>& proc)
    {
        char* line = nullptr;
        while ((line = readline("ldb> ")) != nullptr)
        {
            std::string line_str;

            if (line == std::string_view{""})
            {
                free(line);
                if (history_length > 0)
                {
                    line_str = history_list()[history_length - 1]->line;
                }
            }
            else
            {
                line_str = line;
                add_history(line);
                free(line);
            }
            if (!line_str.empty())
            {
                try
                {
                    handle_command(proc, line_str);
                }
                catch (const ldb::error& err)
                {
                    std::cout << err.what() << std::endl;
                }
            }
        }
    }
} // namespace

int main(int argc, const char** argv)
{
    if (argc == 1)
    {
        std::cerr << "No arguments given\n";
        return -1;
    }
    try
    {
        auto process = attach(argc, argv);
        if (!process)
        {
            fmt::print(stderr, "Failed to attach or launch process.\n");
            return -1;
        }
        g_ldb_process = process.get();

        signal(SIGINT, handle_sigint);
        main_loop(process);
    }
    catch (const ldb::error& err)
    {
        std::cout << err.what() << std::endl;
    }
}
