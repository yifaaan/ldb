#include <algorithm>
#include <csignal>
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
#include <libldb/syscalls.hpp>
#include <libldb/target.hpp>

namespace
{
    ldb::Process* g_LdbProcess = nullptr;

    void HandleSigint(int)
    {
        kill(g_LdbProcess->Pid(), SIGSTOP);
    }

    /// ldb <program name>
    ///
    /// ldb -p <pid>
    std::unique_ptr<ldb::Target> Attach(int argc, const char** argv)
    {

        pid_t pid = 0;
        if (argc == 3 && argv[1] == std::string_view("-p"))
        {
            pid = std::atoi(argv[2]);
            // return ldb::Process::Attach(pid);
            return ldb::Target::Attach(pid);
        }
        else
        {
            const char* programPath = argv[1];
            // return ldb::Process::Launch(programPath, true, std::nullopt);
            // auto proc = ldb::Process::Launch(programPath);
            // fmt::print("Launched process with PID {}\n", proc->Pid());

            auto target = ldb::Target::Launch(programPath);
            fmt::print("Launched process with PID {}\n", target->GetProcess().Pid());
            return target;
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

    std::string GetSigtrapInfo(const ldb::Process& process, ldb::StopReason reason)
    {
        if (reason.trapReason == ldb::TrapType::SoftwareBreak)
        {
            auto& site = process.BreakPointSites().GetByAddress(process.GetPc());
            return fmt::format(" (breakpoint {})", site.Id());
        }

        if (reason.trapReason == ldb::TrapType::HardwareBreak)
        {
            auto id = process.GetCurrentHardwareStoppoint();
            if (id.index() == 0)
            {
                return fmt::format(" (breakpoint {})", std::get<0>(id));
            }

            std::string message;
            auto& point = process.Watchpoinst().GetById(std::get<1>(id));
            message += fmt::format(" (watchpoint {})", point.Id());

            if (point.Data() == point.PreviousData())
            {
                message += fmt::format("\nValue: {:#x}", point.Data());
            }
            else
            {
                message += fmt::format("\nOld value: {:#x}\nNew value: {:#x}", point.PreviousData(), point.Data());
            }
            return message;
        }

        if (reason.trapReason == ldb::TrapType::SingleStep)
        {
            return " (single step)";
        }

        if (reason.trapReason == ldb::TrapType::Syscall)
        {
            const auto& info = *reason.syscallInfo;
            std::string message = " ";
            if (info.entry)
            {
                message += "(syscall entry)\n";
                message += fmt::format("syscall: {}({:#x})", ldb::SyscallIdToName(info.id), fmt::join(info.args, ","));
            }
            else
            {
                message += "(syscall exit)\n";
                message += fmt::format("syscall returned: {:#x}", info.ret);
            }
            return message;
        }
        return "";
    }

    std::string GetSignalStopReason(const ldb::Target& target, ldb::StopReason reason)
    {
        auto& process = target.GetProcess();
        auto message = fmt::format("stopped with signal {} at {:#x}", sigabbrev_np(reason.info), process.GetPc().Addr());

        auto func = target.GetElf().GetSymbolContainingAddress(process.GetPc());
        if (func and ELF64_ST_TYPE(func.value()->st_info) == STT_FUNC)
        {
            message += fmt::format(" ({})", target.GetElf().GetString(func.value()->st_name));
        }
        if (reason.info == SIGTRAP)
        {
            message += GetSigtrapInfo(process, reason);
        }
        return message;
    }

    void PrintStopReason(const ldb::Target& target, ldb::StopReason reason)
    {
        auto& process = target.GetProcess();
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
                // message = fmt::format("stopped with signal {} at {:#x}", sigabbrev_np(reason.info), process.GetPc().Addr());
                // if (reason.info == SIGTRAP)
                // {
                //     message += GetSigtrapInfo(process, reason);
                // }
                message = GetSignalStopReason(target, reason);
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
                                "memory      - Commands for operating on memory\n"
                                "watchpoint  - Commands for operating on watchpoints\n"
                                "catchpoint  - Commands for operating on catchpoints\n"
                                );
        }
        else if (IsPrefix(args[1], "register"))
        {
            std::cerr << fmt::format(
                                "Available commands:\n"
                                "read\n"
                                "read <register>\n"
                                "read all\n"
                                "write <register> <value>\n"
                                );
        }
        else if (IsPrefix(args[1], "breakpoint"))
        {
            std::cerr << fmt::format(
                                "Available commands:\n"
                                "list\n"
                                "delete <id>\n"
                                "disable <id>\n"
                                "enable <id>\n"
                                "set <address> -h\n"
                                );
        }
        else if (IsPrefix(args[1], "memory"))
        {
            std::cerr << fmt::format(
                                "Available commands:\n"
                                "read <address>\n"
                                "read <address> <number of bytes>\n"
                                "write <address>\n"
                                );
        }
        else if (IsPrefix(args[1], "disassemble"))
        {
            std::cerr << fmt::format(
                                "Available commands:\n"
                                "-c <number of instructions>\n"
                                "-a <start address>\n"
                                );
        }
        else if (IsPrefix(args[1], "watchpoint"))
        {
            std::cerr << fmt::format(
                                "Available commands:\n"
                                "list\n"
                                "enable <id>\n"
                                "disable <id>\n"
                                "set <address> <write|rw|execute> <size>\n"
                                );
        }
        else if (IsPrefix(args[1], "catchpoint"))
        {
            std::cerr << fmt::format(
                                "Available commands:\n"
                                "syscall\n"
                                "syscall none\n"
                                "syscall <list of syscall IDs or names>\n"
                                );
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

        for (const auto& instr : instructions)
        {
            fmt::print("{:#018x}: {}\n", instr.address.Addr(), instr.text);
        }
    }

    void HandleStop(ldb::Target& target, ldb::StopReason reason)
    {
        PrintStopReason(target, reason);
        if (reason.reason == ldb::ProcessState::Stopped)
        {
            PrintDisassembly(target.GetProcess(), target.GetProcess().GetPc(), 5);
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

    /// watchpoint list
    void HandleWatchpointList(ldb::Process& process, const std::vector<std::string>& args)
    {
        auto StoppointModeToString = [](auto mode)
        {
            switch (mode)
            {
            case ldb::StoppointMode::Execute: return "Execute";
            case ldb::StoppointMode::Write: return "Write";
            case ldb::StoppointMode::ReadWrite: return "ReadWrite";
            default: ldb::Error::Send("Invalid stoppoint mode");
            }
        };


        if (process.Watchpoints().Empty())
        {
            fmt::print("No watchpoint set\n");
        }
        else
        {
            fmt::print("Current watchpoints:\n");
            process.Watchpoints().ForEach([=](const auto& point)
            {
                fmt::print(
                        "{}: address = {:#x}, mode = {}, size = {}, {}\n",
                        point.Id(),
                        point.Address().Addr(),
                        StoppointModeToString(point.Mode()),
                        point.Size(),
                        point.IsEnabled() ? "enabled" : "disabled");
            });
        }
    }

    /// watchpoint set <address> <mode> <size>
    void HandleWatchpointSet(ldb::Process& process, const std::vector<std::string>& args)
    {
        if (args.size() != 5)
        {
            PrintHelp({"help", "watchpoint"});
            return;
        }

        auto address = ldb::ToIntegral<std::uint64_t>(args[2], 16);
        auto modeText = args[3];
        auto size = ldb::ToIntegral<std::size_t>(args[4]);

        if (!address or !size or !(modeText == "write" or modeText == "rw" or modeText == "execute"))
        {
            PrintHelp({"help", "watchpoint"});
            return;
        }

        ldb::StoppointMode mode;
        if (modeText == "write") mode = ldb::StoppointMode::Write;
        else if (modeText == "rw") mode = ldb::StoppointMode::ReadWrite;
        else if (modeText == "execute") mode = ldb::StoppointMode::Execute;
        else ldb::Error::Send("Invalid mode string");

        process.CreateWatchpoint(ldb::VirtAddr{ *address }, mode, *size).Enable();
    }

    /// watchpoint set <address> <mode> <size>
    ///
    /// watchpoint enable <id>
    ///
    /// watchpoint disable <id>
    ///
    /// watchpoint delete <id>
    ///
    /// watchpoint list
    void HandleWatchpointCommand(ldb::Process& process, const std::vector<std::string>& args)
    {
        if (args.size() < 2)
        {
            PrintHelp({"help", "watchpoint"});
            return;
        }

        const auto& command = args[1];

        if (IsPrefix(command, "list"))
        {
            HandleWatchpointList(process, args);
            return;
        }

        if (args.size() < 3)
        {
            PrintHelp({"help", "watchpoint"});
            return;
        }

        if (IsPrefix(command, "set"))
        {
            HandleWatchpointSet(process, args);
            return;
        }

        auto id = ldb::ToIntegral<ldb::Watchpoint::IdType>(args[2]);
        if (!id)
        {
            std::cerr << fmt::format("Command expects watchpoint id");
            return;
        }

        if (IsPrefix(command, "enable"))
        {
            process.Watchpoints().GetById(*id).Enable();
        }
        else if (IsPrefix(command, "disable"))
        {
            process.Watchpoints().GetById(*id).Disable();
        }
        else if (IsPrefix(command, "delete"))
        {
            process.Watchpoints().RemoveById(*id);
        }
    }

    void HandleSyscallCatchpointCommand(ldb::Process& process, std::vector<std::string>& args)
    {
        auto policy = ldb::SyscallCatchPolicy::CatchAll();

        if (args.size() == 3 and args[2] == "none")
        {
            policy = ldb::SyscallCatchPolicy::CatchNone();
        }
        else if (args.size() >= 3)
        {
            auto syscalls = Split(args[2], ',');
            std::vector<int> toCatch;

            std::transform(std::begin(syscalls), std::end(syscalls), std::back_inserter(toCatch), [&syscalls](const auto& syscall)
            {
                return std::isdigit(syscall[0]) ? ldb::ToIntegral<int>(syscall).value() : ldb::SyscallNameToId(syscall);
            });
            policy = ldb::SyscallCatchPolicy::CatchSome(std::move(toCatch));
        }
        process.SetSyscallCatchPolicy(std::move(policy));
    }

    /// catchpoint syscall
    ///
    /// catchpoint syscall none
    ///
    /// catchpoint syscall <list>
    void HandleCatchpointCommand(ldb::Process& process, std::vector<std::string>& args)
    {
        if (args.size() < 2)
        {
            PrintHelp({ "help", "catchpoint" });
            return;
        }

        if (IsPrefix(args[1], "syscall"))
        {
            HandleSyscallCatchpointCommand(process, args);
        }
    }

    void HandleCommand(std::unique_ptr<ldb::Target>& target, std::string_view line)
    {
        auto args = Split(line, ' ');
        auto command = args[0];
        auto process = &target->GetProcess();

        if (IsPrefix(command, "continue"))
        {
            process->Resume();
            auto reason = process->WaitOnSignal();
            HandleStop(*target, reason);
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
            HandleStop(*target, reason);
        }
        else if (IsPrefix(command, "memory"))
        {
            HandleMemoryCommand(*process, args);
        }
        else if (IsPrefix(command, "disassemble"))
        {
            HandleDisassembleCommand(*process, args);
        }
        else if (IsPrefix(command, "watchpoint"))
        {
            HandleWatchpointCommand(*process, args);
        }
        else if (IsPrefix(command, "catchpoint"))
        {
            HandleCatchpointCommand(*process, args);
        }
        else
        {
            std::cerr << "Unknown command\n";
        }
    }

    void MainLoop(std::unique_ptr<ldb::Target>& target)
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
                    HandleCommand(target, lineStr);
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
        // auto process = Attach(argc, argv);
        // g_LdbProcess = process.get();
        // signal(SIGINT, HandleSigint);
        // MainLoop(process);

        auto target = Attach(argc, argv);
        g_LdbProcess = &target->GetProcess();
        signal(SIGINT, HandleSigint);
        MainLoop(target);
    }
    catch (const ldb::Error& err)
    {
        std::cout << err.what() << '\n';
    }

    // wait for the child to stop after we attach to it
}