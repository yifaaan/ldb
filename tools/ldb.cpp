#include <iostream>
#include <string_view>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <unistd.h>

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
}