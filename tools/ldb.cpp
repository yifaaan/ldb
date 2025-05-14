#include <cstdlib>
#include <iostream>
#include <libldb/libldb.hpp>
#include <readline/history.h>
#include <readline/readline.h>
#include <string_view>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace
{
    std::vector<std::string> split(std::string_view str, char delimiter);
    void resume(pid_t pid);
    void wait_on_signal(pid_t pid);

    pid_t attach(int argc, const char** argv)
    {
        pid_t pid = 0;
        if (argc == 3 && argv[1] == std::string_view{"-p"})
        {
            pid = std::atoi(argv[2]);
            if (pid <= 0)
            {
                std::cerr << "Invalid PID: " << argv[2] << "\n";
                return -1;
            }
            if (ptrace(PTRACE_ATTACH, pid, /*address*/ nullptr, /*data*/ nullptr) < 0)
            {
                std::perror("Could not attach");
                return -1;
            }
        }
        else
        {
            auto program_path = argv[1];
            if (pid = fork(); pid < 0)
            {
                std::perror("fork failed");
                return -1;
            }
            if (pid == 0)
            {
                // Children process
                if (ptrace(PTRACE_TRACEME, 0, /*address*/ nullptr, /*data*/ nullptr) < 0)
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

    void handle_command(pid_t pid, std::string_view line)
    {
    }

} // namespace

int main(int argc, const char** argv)
{
    if (argc == 1)
    {
        std::cerr << "No arguments given\n";
        return -1;
    }
    pid_t pid = attach(argc, argv);

    int wait_status;
    int options = 0;
    if (waitpid(pid, &wait_status, options) < 0)
    {
        std::perror("waitpid failed");
    }
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
            handle_command(pid, line);
        }
    }
}
