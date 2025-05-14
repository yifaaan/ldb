#include <cstdio>
#include <readline/history.h>
#include <readline/readline.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <unistd.h>


#include <cstdlib>
#include <cstring>
#include <iostream>
#include <libldb/libldb.hpp>
#include <ranges>
#include <string_view>
#include <vector>

#include "libldb/error.hpp"
#include "libldb/process.hpp"

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

    void print_stop_reason(const ldb::process& proc, ldb::stop_reason reason)
    {
        std::cout << "Process " << proc.pid() << " ";
        // waitpid之后，进程所处状态
        switch (reason.reason)
        {
        case ldb::process_state::exited:
            std::cout << "Exited with status " << reason.info;
            break;
        case ldb::process_state::terminated:
            std::cout << "Terminated with signal " << sigabbrev_np(reason.info);
            break;
        case ldb::process_state::stopped:
            std::cout << "Stopped with signal " << sigabbrev_np(reason.info);
            break;
        default:
            std::cout << "Unknown stop reason";
            break;
        }
        std::cout << std::endl;
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
        if (command.starts_with("continue"))
        {

            proc->resume();
            auto reason = proc->wait_on_signal();
            print_stop_reason(*proc, reason);
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
