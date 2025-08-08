#include <libldb/Process.h>

#include <libldb/Error.h>

#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace ldb
{
    std::unique_ptr<Process> Process::Launch(std::filesystem::path path)
    {
        pid_t pid;
        if ((pid = fork()) < 0)
        {
            Error::SendErrno("fork failed");
        }
        if (pid == 0)
        {
            if (ptrace(PTRACE_TRACEME, 0, nullptr, nullptr) < 0)
            {
                Error::SendErrno("Tracing failed");
            }
            if (execlp(path.c_str(), path.c_str(), nullptr) < 0)
            {
                Error::SendErrno("exec failed");
            }
        }

        std::unique_ptr<Process> process(new Process(pid, true));
        process->WaitOnSignal();
        return process;
    }

    std::unique_ptr<Process> Process::Attach(pid_t pid)
    {
        if (pid == 0)
        {
            Error::Send("Invalid PID");
        }
        if (ptrace(PTRACE_ATTACH, pid, nullptr, nullptr) < 0)
        {
            Error::SendErrno("Could not attach");
        }

        std::unique_ptr<Process> process(new Process(pid, false));
        process->WaitOnSignal();
        return process;
    }
} // namespace ldb