#include <cstring>
#include <iostream>
#include <sstream>
#include <vector>

#include <editline/readline.h>
#include <fmt/format.h>
#include <fmt/ranges.h>

#include <libldb/Process.h>
#include <libldb/libldb.hpp>
#include <libldb/Error.h>

#include <sys/ptrace.h>
#include <sys/wait.h>

namespace
{

    void PrintStopReason(const ldb::Process& process, ldb::StopReason reason)
    {
        std::cout << "Process " << process.Pid() << ' ';
        // Print the process stop reason
        switch (reason.reason)
        {
        case ldb::ProcessState::exited:
            std::cout << "exited with status" << static_cast<int>(reason.info);
            break;
        case ldb::ProcessState::terminated:
            std::cout << "terminated with signal " << sigabbrev_np(reason.info);
            break;
        case ldb::ProcessState::stopped:
            std::cout << "stopped with signal" << sigabbrev_np(reason.info);
            break;
        }
        std::cout << std::endl;
    }

    std::unique_ptr<ldb::Process> Attach(int argc, const char** argv)
    {
        if (argc == 3 && argv[1] == std::string_view("-p"))
        {
            pid_t pid = std::atoi(argv[2]);
            return ldb::Process::Attach(pid);
        }
        else
        {
            const char* program_path = argv[1];
            return ldb::Process::Launch(program_path);
        }
    }

    std::vector<std::string> Split(std::string_view str, char delimiter)
    {
        std::vector<std::string> out;
        std::stringstream ss{std::string{str}};
        std::string item;
        while (std::getline(ss, item, delimiter))
        {
            out.push_back(item);
        }
        return out;
    }

    bool IsPrefix(std::string_view str, std::string_view of)
    {
        if (str.size() > of.size())
        {
            return false;
        }
        return std::equal(str.begin(), str.end(), of.begin());
    }

    void PrintHelp(const std::vector<std::string>& args)
    {
        if (args.size() == 1)
        {
            std::cerr << R"(Available command:
continue - Resume the process
register
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

        if (args.size() == 2 || (args.size() == 3 && args[2] == "all"))
        {
            for (auto& info : ldb::registerInfos)
            {
                auto shouldPrint = (args.size() == 3 || info.type == ldb::RegisterType::Gpr) && info.name != "orig_rax";
                if (!shouldPrint)
                {
                    continue;
                }
                auto value = process.GetRegisters().Read(info);
                fmt::print("{}:\t{}\n", info.name, std::visit(format, value));
            }
        }
        else if (args.size() == 3)
        {
            try
            {
                auto info = ldb::RegisterInfoByName(args[2]);
                auto value = process.GetRegisters().Read(info);
                fmt::print("{}:\t{}\n", info.name, std::visit(format, value));
            }
            catch (const ldb::Error& e)
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
            // HandleRegisterWrite(process, args);
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
        char* line = nullptr;
        while ((line = readline("ldb> ")) != nullptr)
        {
            std::string lineString;
            if (line == std::string_view(""))
            {
                free(line);
                if (history_length > 0)
                {
                    lineString = history_list()[history_length - 1]->line;
                }
            }
            else
            {
                lineString = line;
                add_history(line);
                free(line);
            }
            if (!lineString.empty())
            {
                try
                {
                    HandleCommand(process, lineString);
                }
                catch (const ldb::Error& e)
                {
                    std::cout << e.what() << '\n';
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
        auto process = Attach(argc, argv);
        MainLoop(process);
    }
    catch (const ldb::Error& e)
    {
        std::cerr << e.what() << '\n';
    }
}