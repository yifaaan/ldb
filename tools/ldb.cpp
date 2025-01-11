#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <type_traits>
#include <unistd.h>
#include <vector>

#include <fmt/format.h>
#include <fmt/ranges.h>
#include <libldb/error.hpp>
#include <libldb/parse.hpp>
#include <libldb/disassembler.hpp>
#include <libldb/process.hpp>
#include <readline/history.h>
#include <readline/readline.h>

namespace
{

    
    /// Launches and attaches to the given program name or PID.
    /// Returns the PID of the inferior.
    std::unique_ptr<ldb::process> attach(int argc, const char** argv)
    {
        pid_t pid = 0;
        // Passing PID.
        if (argc == 3 && argv[1] == std::string_view("-p"))
        {
            pid = std::atoi(argv[2]);
            return ldb::process::attach(pid);
        }
        // Passing program name.
        else
        {
            const char* program_path = argv[1];
            auto proc = ldb::process::launch(program_path);
            fmt::print("Launched process with PID {}\n", proc->pid());
            return proc;
        }
    }

    std::vector<std::string> split(std::string_view str, char delimiter)
    {
        std::vector<std::string> out{};
        std::stringstream ss{std::string{str}};
        std::string item;

        while (std::getline(ss, item, delimiter))
        {
            out.push_back(item);
        }
        return out;
    }

    bool is_prefix(std::string_view str, std::string_view of)
    {
        if (str.size() > of.size())
        {
            return false;
        }
        return std::equal(str.begin(), str.end(), of.begin());
    }

    /// Resume the execution of the process.
    void resume(pid_t pid)
    {
        if (ptrace(PTRACE_CONT, pid, nullptr, nullptr) < 0)
        {
            std::cerr << "Couldn't continue\n";
            std::exit(-1);
        }
    }

    void wait_on_signal(pid_t pid)
    {
        int wait_status;
        int options = 0;
        if (waitpid(pid, &wait_status, options) < 0)
        {
            std::perror("Waitpid failed");
            std::exit(-1);
        }
    }

    void print_stop_reason(const ldb::process& process, ldb::stop_reason reason)
    {
        std::string message;
        switch (reason.reason)
        {
        case ldb::process_state::exited:
            message = fmt::format("exited with status {}", static_cast<int>(reason.info));
            break;
        case ldb::process_state::terminated:
            message = fmt::format("terminated with signal {}", sigabbrev_np(reason.info));
            break;
        case ldb::process_state::stopped:
            message = fmt::format("stopped with signal {} at {:#x}", sigabbrev_np(reason.info), process.get_pc().addr());
            break;
        }
        fmt::print("Process {} {}\n", process.pid(), message);
    }

    void print_help(const std::vector<std::string>& args)
    {
        if (args.size() == 1)
        {
            std::cerr << R"(Available commands:
breakpoint  - Commands for operating on breakpoints
continue    - Resume the process
disassemble - Disassemble machine code to assembly
memory      - Commands for operating on memory
register    - Commands for operating on registers
step        - Step over a single instruction
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
delete <id>
disable <id>
enable <id>
set <address>
set <address> -h
)";
        }
        else if (is_prefix(args[1], "memory"))
        {
            std::cerr << R"(Available commands:
read <address>
read <address> <number of bytes>
write <address> <bytes>
)";
        }
        else if (is_prefix(args[1], "disassemble"))
        {
            std::cerr << R"(Available commands:
-c <number of instructions>
-a <start address>
)";
        }
        else
        {
            std::cerr << "No help available on that\n";
        }
    }


    void print_disassembly(ldb::process& process, ldb::virt_addr address, std::size_t n_instructions)
    {
        ldb::disassember dis(process);
        auto instructions = dis.disassemble(n_instructions, address);
        for (auto& instr : instructions)
        {
            fmt::print("{:#018x}: {}\n", instr.address.addr(), instr.text);
        }
    }

    void handle_stop(ldb::process& process, ldb::stop_reason reason)
    {
        print_stop_reason(process, reason);
        if (reason.reason == ldb::process_state::stopped)
        {
            print_disassembly(process, process.get_pc(), 5);
        }    
    }

    void handle_register_read(ldb::process& process, const std::vector<std::string>& args)
    {
        auto format = [](auto t)
        {
            if constexpr (std::is_floating_point_v<decltype(t)>)
            {
                return fmt::format("{}", t);
            }
            else if constexpr (std::is_integral_v<decltype(t)>)
            {
                return fmt::format("{:#0{}x}", t, sizeof(t) * 2 + 2);
            }
            else
            {
                // fmt vector
                return fmt::format("[{:#04x}]", fmt::join(t, ","));
            }
        };
        if (args.size() == 2 or (args.size() == 3 and args[2] == "all"))
        {
            for (auto& info : ldb::g_register_infos)
            {
                auto should_print = (args.size() == 3 or info.type == ldb::register_type::gpr) and info.name != "orig_rax";
                if (!should_print)
                {
                    continue;
                }
                auto value = process.get_registers().read(info);
                fmt::print("{}:\t{}\n", info.name, std::visit(format, value));
            }
        }
        else if (args.size() == 3)
        {
            try
            {
                // search for the sdb::register_info value with the given name
                auto info = ldb::register_info_by_name(args[2]);
                auto value = process.get_registers().read(info);
                fmt::print("{}:\t{}\n", info.name, std::visit(format, value));
            }
            catch (ldb::error& err)
            {
                std::cerr << "No such register\n";
                return;
            }
        }
        else
        {
            print_help({"help", "register"});
        }
    }

    ldb::registers::value parse_register_value(ldb::register_info info, std::string_view text)
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
                // fmt::print("in double_float");
                return ldb::to_float<double>(text).value();
            }
            else if (info.format == ldb::register_format::vector)
            {
                // fmt::print("in vector");
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
            ldb::error::send("Invalid format");
        }
    }

    void handle_register_write(ldb::process& process, const std::vector<std::string>& args)
    {
        if (args.size() != 4)
        {
            print_help({"help", "register"});
            return;
        }
        try
        {
            auto info = ldb::register_info_by_name(args[2]);
            // parse the supplied value
            auto value = parse_register_value(info, args[3]);
            process.get_registers().write(info, value);
        }
        catch (ldb::error& err)
        {
            std::cerr << err.what() << '\n';
            return;
        }
    }

    void handle_register_command(ldb::process& process, const std::vector<std::string>& args)
    {
        if (args.size() < 2)
        {
            print_help({"help", "register"});
            return;
        }
        if (is_prefix(args[1], "read"))
        {
            handle_register_read(process, args);
        }
        else if (is_prefix(args[1], "write"))
        {
            handle_register_write(process, args);
        }
        else
        {
            print_help({"help", "register"});
        }
    }

    void handle_breakpoint_command(ldb::process& process, const std::vector<std::string>& args)
    {
        if (args.size() < 2)
        {
            print_help({"help", "breakpoint"});
            return;
        }

        auto command = args[1];

        if (is_prefix(command, "list"))
        {
            if (process.breakpoint_sites().empty())
            {
                fmt::print("No breakpoints set\n");
            }
            else
            {
                fmt::print("Current breakpoints:\n");
                process.breakpoint_sites().for_each(
                    [](auto& site)
                    {
                        if (site.is_internal()) return;
                        fmt::print("{}: address = {:#x}, {}\n", site.id(), site.address().addr(), site.is_enabled() ? "enabled" : "disabled");
                    });
            }
            return;
        }

        if (args.size() < 3)
        {
            print_help({"help", "breakpoint"});
            return;
        }

        if (is_prefix(command, "set"))
        {
            auto address = ldb::to_integral<std::uint64_t>(args[2], 16);

            if (!address)
            {
                fmt::print(stderr, "Breakpoint command expects address in " "hexadecimal, prefixed with '0x'\n");
                return;
            }
            bool hardware = false;
            if (args.size() == 4)
            {
                if (args[3] == "-h")
                {
                    hardware = true;
                }
                else
                {
                    ldb::error::send("Invalid breakpoint command argument");
                }
            }
            process.create_breakpoint_site(ldb::virt_addr{*address}, hardware).enable();
            return;
        }

        auto id = ldb::to_integral<ldb::breakpoint_site::id_type>(args[2]);
        if (!id)
        {
            std::cerr << "Command expects breakpoint id";
            return;
        }

        if (is_prefix(command, "enable"))
        {
            process.breakpoint_sites().get_by_id(*id).enable();
        }
        else if (is_prefix(command, "disable"))
        {
            process.breakpoint_sites().get_by_id(*id).disable();
        }
        else if (is_prefix(command, "delete"))
        {
            process.breakpoint_sites().remove_by_id(*id);
        }
    }

    /**
     * @brief memory read <address>
     * default to 32 bytes
     * @param process
     * @param args
     */
    void handle_memory_read_command(ldb::process& process, const std::vector<std::string>& args)
    {
        auto address = ldb::to_integral<std::uint64_t>(args[2], 16);
        if (!address)
            ldb::error::send("Invalid address format");

        auto n_bytes = 32;
        if (args.size() == 4)
        {
            auto bytes_arg = ldb::to_integral<std::size_t>(args[3]);
            if (!bytes_arg)
                ldb::error::send("Invalid number of bytes");
            n_bytes = *bytes_arg;
        }
        auto data = process.read_memory(ldb::virt_addr{*address}, n_bytes);
        for (std::size_t i = 0; i < data.size(); i += 16)
        {
            auto start = data.begin() + i;
            auto end = data.begin() + std::min(i + 16, data.size());
            fmt::print("{:#016x}: {:02x}\n", *address + i, fmt::join(start, end, " "));
        }
    }

    /**
     * @brief memory write <address> <value>
     * 
     * @param process 
     * @param args 
     */
    void handle_memory_write_command(ldb::process& process, const std::vector<std::string>& args)
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
        process.write_memory(ldb::virt_addr{*address}, {data.data(), data.size()});
    }



    /**
     * @brief memory write <address> <values>,
     * values format: [0xff, 0xff, ...]
     * @param process
     * @param args
     */
    void handle_memory_command(ldb::process& process, const std::vector<std::string>& args)
    {
        if (args.size() < 3)
        {
            print_help({"help", "memory"});
            return;
        }
        if (is_prefix(args[1], "read"))
        {
            handle_memory_read_command(process, args);
        }
        else if (is_prefix(args[1], "write"))
        {
            handle_memory_write_command(process, args);
        }
        else
        {
            print_help({"help", "memory"});
        }
    }

    /**
     * @brief disassemble -c <n_instructions> -a <address>
     * 
     * @param process 
     * @param args 
     */
    void handle_disassemble_command(ldb::process& process, const std::vector<std::string>& args)
    {
        auto address = process.get_pc();
        std::size_t n_instructions = 5;
        auto it = args.begin() + 1;
        while (it != args.end())
        {
            if (*it == "-a" and it + 1 != args.end())
            {
                it++;
                auto opt_addr = ldb::to_integral<std::uint64_t>(*it++, 16);
                if (!opt_addr)
                {
                    ldb::error::send("Invalid address format");
                    address = ldb::virt_addr{*opt_addr};
                }
            }
            else if (*it == "-c" and it + 1 != args.end())
            {
                it++;
                auto opt_n = ldb::to_integral<std::size_t>(*it++);
                if (!opt_n)
                {
                    ldb::error::send("Invalid instruction count");
                    n_instructions = *opt_n;
                }
            }
            else 
            {
                print_help({"help", "disassemble"});
                return;
            }
        }
        print_disassembly(process, address, n_instructions);
    }
    void handle_command(std::unique_ptr<ldb::process>& process, std::string_view line)
    {
        auto args = split(line, ' ');
        auto command = args[0];

        if (is_prefix(command, "continue"))
        {
            process->resume();
            auto reason = process->wait_on_signal();
            handle_stop(*process, reason);
        }
        else if (is_prefix(command, "help"))
        {
            print_help(args);
        }
        else if (is_prefix(command, "register"))
        {
            handle_register_command(*process, args);
        }
        else if (is_prefix(command, "breakpoint"))
        {
            handle_breakpoint_command(*process, args);
        }
        else if (is_prefix(command, "step"))
        {
            auto reason = process->step_instruction();
            handle_stop(*process, reason);
        }
        else if (is_prefix(command, "memory"))
        {
            handle_memory_command(*process, args);
        }
        else if (is_prefix(command, "disassemble"))
        {
            handle_disassemble_command(*process, args);
        }
        else
        {
            std::cerr << "Unknown command\n";
        }
    }

    void main_loop(std::unique_ptr<ldb::process>& process)
    {
        char* line = nullptr;
        // Reading input from user.
        while ((line = readline("ldb> ")) != nullptr)
        {
            std::string line_str;
            // If is an empty line.
            // Re-run the last command.
            if (line == std::string_view(""))
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
                    handle_command(process, line_str);
                }
                catch (const ldb::error& err)
                {
                    std::cout << err.what() << '\n';
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
        std::cout << err.what() << '\n';
    }
}
