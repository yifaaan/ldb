#include <iostream>
#include <sstream>
#include <vector>

#include <editline/readline.h>
#include <libldb/libldb.hpp>

#include <sys/ptrace.h>
#include <sys/wait.h>

namespace
{
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
            // Attach to the process, kernel sends SIGSTOP to the process
            if (ptrace(PTRACE_ATTACH, pid, nullptr, nullptr) < 0)
            {
                std::perror("Could not attach");
                return -1;
            }
        }
        else
        {
            const char* program_path = argv[1];
            if ((pid = fork()) < 0)
            {
                std::perror("fork failed");
                return -1;
            }
            if (pid == 0)
            {
                if (ptrace(PTRACE_TRACEME, 0, nullptr, nullptr) < 0)
                {
                    std::perror("Tracing failed");
                    return -1;
                }
                if (execlp(program_path, program_path, nullptr) < 0)
                {
                    std::perror("Exec failed");
                    return -1;
                }
            }
        }
        return pid;
    }

    void HandleCommand(pid_t pid, std::string_view line);

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
    void Resume(pid_t pid)
    {
        if (ptrace(PTRACE_CONT, pid, nullptr, nullptr) < 0)
        {
            std::cerr << "Couldn't continue\n";
            std::exit(-1);
        }
    }

    void WaitOnSignal(pid_t pid)
    {
        int wait_status;
        int options = 0;
        if (waitpid(pid, &wait_status, options) < 0)
        {
            std::perror("waitpid failed");
            std::exit(-1);
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
} // namespace

int main(int argc, const char** argv)
{
    if (argc < 2)
    {
        std::cerr << "No arguments given\n";
        return -1;
    }
    auto pid = Attach(argc, argv);
    int waitStatus;
    int options = 0;
    if (waitpid(pid, &waitStatus, options) < 0)
    {
        std::perror("Waitpid failed");
    }

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
            HandleCommand(pid, lineString);
        }
    }
}