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
            return ldb::Process::Launch(programPath);
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
            std::cerr << R"(Available commands:
continue    - Resume the process
register    - Commands for operating on registers
)";
        }
        else if (IsPrefix(args[1], "register"))
        {
            std::cerr << R"(Available commands:
read
read <register>
read all
write <register> <value>
)";
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

    void HandleCommand(std::unique_ptr<ldb::Process>& process, std::string_view line)
    {
        auto args = Split(line, ' ');
        auto command = args[0];

        if (IsPrefix(command, "continue"))
        {
            process->Resume();
            auto reason = process->WaitOnSignal();
            PrintStopReason(*process, reason);
        }
        else if (IsPrefix(command, "help"))
        {
            PrintHelp(args);
        }
        else if (IsPrefix(command, "register"))
        {
            HandleRegisterCommand(*process, args);
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