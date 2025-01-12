
#include <csignal>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <libldb/process.hpp>
#include <libldb/error.hpp>


ldb::StopReason::StopReason(int waitStatus)
{
    if (WIFEXITED(waitStatus))
    {
        reason = ProcessState::Exited;
        info = WEXITSTATUS(waitStatus);
    }
    else if (WIFSIGNALED(waitStatus))
    {
        reason = ProcessState::Terminated;
        info = WTERMSIG(waitStatus);
    }
    else if (WIFSTOPPED(waitStatus))
    {
        reason = ProcessState::Stopped;
        info = WSTOPSIG(waitStatus);
    }
}

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

ldb::Process::~Process()
{
    if (pid != 0)
    {
        int status;
        if (state == ProcessState::Running)
        {
            kill(pid, SIGSTOP);
            waitpid(pid, &status, 0);
        }

        ptrace(PTRACE_DETACH, pid, nullptr, nullptr);
        kill(pid, SIGCONT);

        if (terminateOnEnd)
        {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
        }
    }
}

void ldb::Process::Resume()
{
    if (ptrace(PTRACE_CONT, pid, nullptr, nullptr) < 0)
    {
        Error::SendErrno("Could not resume");
    }
    state = ProcessState::Running;
}

ldb::StopReason ldb::Process::WaitOnSignal()
{
    int waitStatus;
    int options = 0;
    if (waitpid(pid, &waitStatus, options) < 0)
    {
        Error::SendErrno("waitpid failed");
    }
    StopReason reason(waitStatus);
    // update traced process's state
    state = reason.reason;
    return reason;
}