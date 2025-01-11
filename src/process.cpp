
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <libldb/process.hpp>
#include <libldb/error.hpp>

std::unique_ptr<ldb::Process> ldb::Process::Launch(std::filesystem::path path)
{
    pid_t pid;
    if ((pid = fork()) < 0)
    {
        Error::SendErrno("fork failed");
    }

    if (pid == 0)
    {
        // in child
        // set itself up to be traced
        if (ptrace(PTRACE_TRACEME, 0, nullptr, nullptr) < 0)
        {
            Error::SendErrno("Tracing failed");
        }
        // kernal will stop the process on a call to exec if it's being traced using ptrace
        if (execlp(path.c_str(), path.c_str(), nullptr) < 0)
        {
            Error::SendErrno("exec failed");
        }
    }

    // in parent
    std::unique_ptr<Process> childProc(new Process(pid, true));
    childProc->WaitOnSignal();
    return childProc;
}

std::unique_ptr<ldb::Process> ldb::Process::Attach(pid_t pid)
{
    if (pid == 0)
    {
        Error::Send("Invalid PID");
    }
    if (ptrace(PTRACE_ATTACH, pid, nullptr, nullptr) < 0)
    {
        Error::SendErrno("Could not attach");
    }

    std::unique_ptr<Process> beAttachedProc(new Process(pid, false));
    beAttachedProc->WaitOnSignal();
    return beAttachedProc;
}