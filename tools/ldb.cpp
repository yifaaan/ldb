#include "libldb/error.hpp"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <sstream>
#include <vector>
#include <string_view>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <unistd.h>

#include <readline/readline.h>
#include <readline/history.h>
#include <libldb/process.hpp>

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
        std::cout << "Process " << process.Pid() << ' ';

        switch (reason.reason)
        {
            case ldb::ProcessState::Exited:
                std::cout << "exited with status "
                        << static_cast<int>(reason.info);
                break;
            case ldb::ProcessState::Terminated:
                std::cout << "terminated with signal "
                        << sigabbrev_np(reason.info);
                break;
            case ldb::ProcessState::Stopped:
                std::cout << "stopped with signal "
                        << sigabbrev_np(reason.info);
                break;
        }
        std::cout << std::endl;
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

            
            if (lineStr == std::string_view(""))
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
                    HandleCommand(process, line);
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