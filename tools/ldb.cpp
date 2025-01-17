#include <algorithm>
#include <cstring>
#include <iostream>
#include <sstream>
#include <vector>
#include <string_view>
#include <variant>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <unistd.h>

#include <readline/readline.h>
#include <readline/history.h>
#include <fmt/format.h>
#include <fmt/ranges.h>

#include <libldb/process.hpp>
#include <libldb/error.hpp>
#include <libldb/parse.hpp>
#include <libldb/disassembler.hpp>

namespace
{
    /// ldb <program name>
    ///
    /// ldb -p <pid>
    std::unique_ptr<ldb::Process> Attach(int argc, const char** argv)
    {

        pid_t pid = 0;
        if (argc == 3 && argv[1] == std::string_view("-p"))
        {
            pid = std::atoi(argv[2]);
            return ldb::Process::Attach(pid);
        }
        else
        {
            const char* programPath = argv[1];
            // return ldb::Process::Launch(programPath, true, std::nullopt);
            auto proc = ldb::Process::Launch(programPath);
            fmt::print("Launched process with PID {}\n", proc->Pid());
            return proc;
        }
    }

    std::vector<std::string> Split(std::string_view str, char delimiter)
    {
        std::stringstream ss{std::string(str)};
        std::vector<std::string> out;
        std::string s;
        while (std::getline(ss, s, delimiter))
        {
            out.push_back(s);
        }
        return out;
    }

    bool IsPrefix(std::string_view str, std::string_view of)
    {
        if (str.size() > of.size()) return false;
        return std::equal(str.begin(), str.end(), of.begin());
    }

    /// make inferior process continue
    void Resume(pid_t pid)
    {
        if (ptrace(PTRACE_CONT, pid, nullptr, nullptr) < 0)
        {
            std::cerr << "Couldn't continue\n";
            std::exit(1);
        }
    }

    void WaitOnSignal(pid_t pid)
    {
        int waitStatus;
        int options = 0;
        if (waitpid(pid, &waitStatus, options) < 0)
        {
            std::perror("waitpid failed");
            std::exit(1);
        }
    }

    void PrintStopReason(const ldb::Process& process, ldb::StopReason reason)
    {
        std::string message;
        switch (reason.reason)
        {
            case ldb::ProcessState::Exited:
                message = fmt::format("exited with status {}", static_cast<int>(reason.info));
                break;
            case ldb::ProcessState::Terminated:
                message = fmt::format("terminated with signal {}", sigabbrev_np(reason.info));
                break;
            case ldb::ProcessState::Stopped:
                message = fmt::format("stopped with signal {} at {:#x}", sigabbrev_np(reason.info), process.GetPc().Addr());
                break;
        }
        fmt::print("Process {} {}\n", process.Pid(), message);
    }

    void PrintHelp(const std::vector<std::string>& args)
    {
        if (args.size() == 1)
        {
            std::cerr << fmt::format(
                                "Available commands:\n"
                                "continue    - Resume the process\n"
                                "step        - Step over a single instruction\n"
                                "disassemble - Disassemble machine code to assembly\n"
                                "register    - Commands for operating on registers\n"
                                "breakpoint  - Commands for operating on breakpoints\n"
                                "memory      - Commands for operating on memory\n");
        }
        else if (IsPrefix(args[1], "register"))
        {
            std::cerr << fmt::format(
                                "Available commands:\n"
                                "read\n"
                                "read <register>\n"
                                "read all\n"
                                "write <register> <value>\n");
        }
        else if (IsPrefix(args[1], "breakpoint"))
        {
            std::cerr << fmt::format(
                                "Available commands:\n"
                                "list\n"
                                "delete <id>\n"
                                "disable <id>\n"
                                "enable <id>\n"
                                "set <address> -h\n");
        }
        else if (IsPrefix(args[1], "memory"))
        {
            std::cerr << fmt::format(
                                "Available commands:\n"
                                "read <address>\n"
                                "read <address> <number of bytes>\n"
                                "write <address>\n");
        }
        else if (IsPrefix(args[1], "disassemble"))
        {
            std::cerr << fmt::format(
                                "Available commands:\n"
                                "-c <number of instructions>\n"
                                "-a <start address>\n");
        }
        else
        {
            std::cerr << "No help available on that\n";
        }
    }


    /// register read <register>
    ///
    /// register read all
    ///
    /// register read
    void HandleRegisterRead(ldb::Process& process, const std::vector<std::string>& args)
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
                return fmt::format("[{:#04x}]", fmt::join(t, ","));
            }
        };

        // register read all
        // register read
        if (args.size() == 2 or (args.size() == 3 and args[2] == "all"))
        {
            for (const auto& info : ldb::GRegisterInfos)
            {
                auto shouldPrint = (args.size() == 3 or info.type == ldb::RegisterType::Gpr) and info.name != "orig_rax";
                if (!shouldPrint) continue;
                auto value = process.GetRegisters().Read(info);
                fmt::print("{}:\t{}\n", info.name, std::visit(format, value));
            }
        }
        // register read <register>
        else if (args.size() == 3)
        {
            try
            {
                auto info = ldb::RegisterInfoByName(args[2]);
                auto value = process.GetRegisters().Read(info);
                fmt::print("{}:\t{}\n", info.name, std::visit(format, value));
            }
            catch (ldb::Error& err)
            {
                std::cerr << "No such register\n";
                return;
            }
        }
        else
        {
            PrintHelp({"help", "register"});
        }
    }

    ldb::Registers::value ParseRegisterValue(ldb::RegisterInfo info, std::string_view text)
    {
        try
        {
            if (info.format == ldb::RegisterFormat::UInt)
            {
                switch (info.size)
                {
                case 1: return ldb::ToIntegral<std::uint8_t>(text, 16).value();
                case 2: return ldb::ToIntegral<std::uint16_t>(text, 16).value();
                case 4: return ldb::ToIntegral<std::uint32_t>(text, 16).value();
                case 8: return ldb::ToIntegral<std::uint64_t>(text, 16).value();
                }
            }
            else if (info.format == ldb::RegisterFormat::DoubleFloat)
            {
                return ldb::ToFloat<double>(text).value();
            }
            else if (info.format == ldb::RegisterFormat::LongDouble)
            {
                return ldb::ToFloat<long double>(text).value();
            }
            else if (info.format == ldb::RegisterFormat::Vector)
            {
                if (info.size == 8)
                {
                    return ldb::ParseVector<8>(text);
                }
                else if (info.size == 16)
                {
                    return ldb::ParseVector<16>(text);
                }
            }
        }
        catch (...)
        {

        }
        ldb::Error::Send("Invalid format");

    }

    /// register write <register name> <value>
    void HandleRegisterWrite(ldb::Process& process, const std::vector<std::string>& args)
    {
        if (args.size() != 4)
        {
            PrintHelp({"help", "register"});
            return;
        }

        try
        {
            auto info = ldb::RegisterInfoByName(args[2]);
            auto value = ParseRegisterValue(info, args[3]);
            process.GetRegisters().Write(info, value);
        }
        catch (ldb::Error& err)
        {
            std::cerr << err.what() << '\n';
            return;
        }
    }

    /// register read
    ///
    /// register read all
    ///
    /// register read <register name>
    ///
    /// register write <register name> <value>
    void HandleRegisterCommand(ldb::Process& process, const std::vector<std::string>& args)
    {
        if (args.size() < 2)
        {
            PrintHelp({"help", "register"});
            return;
        }

        if (IsPrefix(args[1], "read"))
        {
            HandleRegisterRead(process, args);
        }
        else if (IsPrefix(args[1], "write"))
        {
            HandleRegisterWrite(process, args);
        }
        else
        {
            PrintHelp({"help", "register"});
        }
    }

    void HandleBreakpointCommand(ldb::Process& process, const std::vector<std::string>& args)
    {
        if (args.size() < 2)
        {
            PrintHelp({"help", "breakpoint"});
            return;
        }

        const auto& command = args[1];

        if (IsPrefix(command, "list"))
        {
            if (process.BreakPointSites().Empty())
            {
                fmt::print("No breakpoints set\n");
            }
            else
            {
                fmt::print("Current breakpoints:\n");
                process.BreakPointSites().ForEach([](const auto& site)
                {
                    if (site.IsInternal()) return;
                    fmt::print(
                            "{}: address = {:#x}, {}\n",
                            site.Id(),
                            site.Address().Addr(),
                            site.IsEnabled() ? "enabled" : "disabled");
                });
            }
            return;
        }

        if (args.size() < 3)
        {
            PrintHelp({"help", "breakpoint"});
            return;
        }
        if (IsPrefix(command, "set"))
        {
            auto address = ldb::ToIntegral<std::uint64_t>(args[2], 16);
            if (!address)
            {
                std::cerr << fmt::format(
                                    "Breakpoint command expects address in "
                                    "hexadecimal, prefixed with '0x'\n");

                return;
            }
            bool hardware = false;
            if (args.size() == 4)
            {
                if (args[3] == "-h") hardware = true;
                else ldb::Error::Send("Invalid breakpoint command argument");
            }
            process.CreateBreakpointSite(ldb::VirtAddr{ *address }, hardware).Enable();
            return;
        }

        auto id = ldb::ToIntegral<ldb::BreakpointSite::IdType>(args[2]);
        if (!id)
        {
            std::cerr << fmt::format("Command expects breakpoint id");
            return;
        }

        if (IsPrefix(command, "enable"))
        {
            process.BreakPointSites().GetById(*id).Enable();
        }
        else if (IsPrefix(command, "disable"))
        {
            process.BreakPointSites().GetById(*id).Disable();
        }
        else if (IsPrefix(command, "delete"))
        {
            process.BreakPointSites().RemoveById(*id);
        }
    }

    /// memory read <address>
    ///
    /// memory read <address> <number of bytes>
    void HandleMemoryReadCommand(ldb::Process& process, const std::vector<std::string>& args)
    {
        auto address = ldb::ToIntegral<std::uint64_t>(args[2], 16);
        if (!address)
        {
            ldb::Error::Send("Invalid address format");
        }
        // memory read <address>
        // default to read 32 bytes
        std::size_t nBytes = 32;
        if (args.size() == 4)
        {
            auto byteArg = ldb::ToIntegral<std::size_t>(args[3]);
            if (!byteArg)
            {
                ldb::Error::Send("Invalid number of bytes");
            }
            nBytes = *byteArg;
        }
        auto data = process.ReadMemory(ldb::VirtAddr{*address}, nBytes);
        for (std::size_t i = 0; i < data.size(); i += 16)
        {
            auto start = data.begin() + i;
            auto end = data.begin() + std::min(i + 16, data.size());
            // 0x0055555555515b: cc f0 fe ff ff b8 00 00 00 00 5d c3 00 f3 0f 1e
            // 0x0055555555516b: fa 48 83 ec 08 48 83 c4 08 c3 00 00 00 00 00 00
            fmt::print("{:#016x}: {:02x}\n",
                    *address + i,
                    fmt::join(start, end, " "));
        }
    }

    /// memory write <address> <values>
    void HandleMemoryWriteCommand(ldb::Process& process, const std::vector<std::string>& args)
    {
        if (args.size() != 4)
        {
            PrintHelp({"help", "memory"});
            return;
        }

        auto address = ldb::ToIntegral<std::uint64_t>(args[2], 16);
        if (!address)
        {
            ldb::Error::Send("Invalid address format");
        }
        // mem write 0x555555555156 [0xff,0xff]
        auto data = ldb::ParseVector(args[3]);
        process.WriteMemory(ldb::VirtAddr{*address}, {data.data(), data.size()});
    }

    /// memory read <address>
    ///
    /// memory read <address> <number of bytes>
    ///
    /// memory write <address> <values>
    void HandleMemoryCommand(ldb::Process& process, const std::vector<std::string>& args)
    {
        if (args.size() < 3)
        {
            PrintHelp({"help", "memory"});
            return;
        }
        if (IsPrefix(args[1], "read"))
        {
            HandleMemoryReadCommand(process, args);
        }
        else if (IsPrefix(args[1], "write"))
        {
            HandleMemoryWriteCommand(process, args);
        }
        else
        {
            PrintHelp({"help", "memory"});
        }
    }

    void PrintDisassembly(ldb::Process& process, ldb::VirtAddr address, std::size_t nInstructions)
    {
        ldb::Disassembler dis(process);

        auto instructions = dis.Disassemble(nInstructions, address);
        fmt::println("after dis.Disassemble");
        for (const auto& instr : instructions)
        {
            fmt::print("{:#018x}: {}\n", instr.address.Addr(), instr.text);
        }
    }

    void HandleStop(ldb::Process& process, ldb::StopReason reason)
    {
        PrintStopReason(process, reason);
        if (reason.reason == ldb::ProcessState::Stopped)
        {
            PrintDisassembly(process, process.GetPc(), 5);
        }
    }

    /// disassemble -c <n_instructions> -a <address>
    void HandleDisassembleCommand(ldb::Process& process, const std::vector<std::string>& args)
    {
        auto address = process.GetPc();
        std::size_t nInstructions = 5;

        auto it = args.begin() + 1;
        while (it != args.end())
        {
            if (*it == "-c" and it + 1 != args.end())
            {
                ++it;
                auto optN = ldb::ToIntegral<std::size_t>(*it++);
                if (!optN)
                {
                    ldb::Error::Send("Invalid instruction count");
                }
                nInstructions = *optN;
            }
            else if (*it == "-a" and it + 1 != args.end())
            {
                ++it;
                auto optAddr = ldb::ToIntegral<std::uint64_t>(*it++, 16);
                if (!optAddr)
                {
                    ldb::Error::Send("Invalid address format");
                }
                address = ldb::VirtAddr{*optAddr};
            }
            else
            {
                PrintHelp({"help", "disassemble"});
                return;
            }
        }
        PrintDisassembly(process, address, nInstructions);
    }

    void HandleCommand(std::unique_ptr<ldb::Process>& process, std::string_view line)
    {
        auto args = Split(line, ' ');
        auto command = args[0];

        if (IsPrefix(command, "continue"))
        {
            process->Resume();
            auto reason = process->WaitOnSignal();
            HandleStop(*process, reason);
        }
        else if (IsPrefix(command, "help"))
        {
            PrintHelp(args);
        }
        else if (IsPrefix(command, "register"))
        {
            HandleRegisterCommand(*process, args);
        }
        else if (IsPrefix(command, "breakpoint"))
        {
            HandleBreakpointCommand(*process, args);
        }
        else if (IsPrefix(command, "step"))
        {
            auto reason = process->StepInstruction();
            HandleStop(*process, reason);
        }
        else if (IsPrefix(command, "memory"))
        {
            HandleMemoryCommand(*process, args);
        }
        else if (IsPrefix(command, "disassemble"))
        {
            HandleDisassembleCommand(*process, args);
        }
        else
        {
            std::cerr << "Unknown command\n";
        }
    }

    void MainLoop(std::unique_ptr<ldb::Process>& process)
    {
        // user input cmd
        char* line = nullptr;
        while ((line = readline("ldb> ")) != nullptr)
        {
            std::string lineStr;

            
            if (line == std::string_view(""))
            {
                // empty line: re-run the last command
                free(line);
                if (history_length > 0)
                {
                    lineStr = history_list()[history_length - 1]->line;
                }
            }
            else
            {
                lineStr = line;
                add_history(line);
                free(line);
            }

            if (!lineStr.empty())
            {
                try
                {
                    // fmt::print("in main cmd: {}\n", line);
                    HandleCommand(process, lineStr);
                }
                catch (const ldb::Error& err)
                {
                    std::cout << err.what() << '\n';
                }
            }
        }
    }
}

int main(int argc, const char** argv)
{
    if (argc == 1)
    {
        std::cerr << "No arguments given\n";
        return -1;
    }
    
    try
    {
        auto process = Attach(argc, argv);
        MainLoop(process);
    }
    catch (const ldb::Error& err)
    {
        std::cout << err.what() << '\n';
    }

    // wait for the child to stop after we attach to it
}