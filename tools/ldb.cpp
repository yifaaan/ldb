#include <algorithm>
#include <iostream>
#include <sstream>
#include <string_view>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <unistd.h>

#include <readline/readline.h>
#include <readline/history.h>
#include <vector>

namespace
{
    /// ldb <program name>
    ///
    /// ldb -p <pid>
    pid_t Attach(int argc, const char** argv)
    {

        pid_t pid = 0;
        if (argc == 3 && argv[1] == std::string_view("-p"))
        {
            pid = std::atoi(argv[2]);
            if (pid <= 0)
            {
                std::cerr << "Invalid pid\n";
                return -1;
            }
            // send the process a SIGSTOP to pause its execution
            if (ptrace(PTRACE_ATTACH, pid, nullptr, nullptr) < 0)
            {
                std::perror("Could not attach");
                return -1;
            }
        }
        else
        {
            const char* programPath = argv[1];
            if ((pid = fork()) < 0)
            {
                std::perror("fork failed");
                return -1;
            }
            if (pid == 0)
            {
                // in child
                // set itself up to be traced
                if (ptrace(PTRACE_TRACEME, 0, nullptr, nullptr) < 0)
                {
                    std::perror("Tracing failed");
                    return -1;
                }
                // kernal will stop the process on a call to exec if it's being traced using ptrace
                if (execlp(programPath, programPath, nullptr) < 0)
                {
                    std::perror("Exec failed");
                    return -1;
                }
            }
        }
        return pid;
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

    void HandleCommand(pid_t pid, std::string_view line)
    {
        auto args = Split(line, ' ');
        auto command = args[0];

        if (IsPrefix(command, "continue"))
        {
            Resume(pid);
            WaitOnSignal(pid);
        }
        else
        {
            std::cerr << "Unknown command\n";
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
    pid_t pid = Attach(argc, argv);

    // wait for the child to stop after we attach to it
    int waitStatus;
    int options = 0;
    if (waitpid(pid, &waitStatus, options) < 0)
    {
        std::perror("waitpid failed");
    }

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
            HandleCommand(pid, line);
        }
    }
}