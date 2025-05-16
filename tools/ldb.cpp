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
#include <libldb/error.hpp>
#include <libldb/libldb.hpp>
#include <libldb/parse.hpp>
#include <libldb/process.hpp>
#include <ranges>
#include <string_view>
#include <type_traits>
#include <vector>

namespace
{
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
            enable <id>
            disable <id>
            delete <id>
            )";
        }
        else
        {
            std::cerr << "No help available for this command\n";
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
                proc.breakpoint_sites().for_each([](const auto& site)
                {
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
            proc.create_breakpoint_site(ldb::virt_addr{*address}).enable();
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
            return ldb::process::launch(program_path);
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
            print_stop_reason(*proc, reason);
        }
        else if (is_prefix(command, "register"))
        {
            handle_register_command(*proc, args);
        }
        else if (is_prefix(command, "breakpoint"))
        {
            handle_breakpoint_command(*proc, args);
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
        main_loop(process);
    }
    catch (const ldb::error& err)
    {
        std::cout << err.what() << std::endl;
    }
}
