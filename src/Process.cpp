#include <libldb/Process.h>

#include <libldb/Error.h>
#include <libldb/Pipe.h>

#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace
{
    void ExitWithPerror(ldb::Pipe& channel, const std::string& prefix)
    {
        auto message = prefix + ": " + strerror(errno);
        channel.Write(reinterpret_cast<std::byte*>(message.data()), message.size());
        exit(-1);
    }
} // namespace

namespace ldb
{
    StopReason::StopReason(int waitStatus)
    {
        if (WIFEXITED(waitStatus))
        {
            reason = ProcessState::exited;
            info = WEXITSTATUS(waitStatus);
        }
        else if (WIFSIGNALED(waitStatus))
        {
            reason = ProcessState::terminated;
            info = WTERMSIG(waitStatus);
        }
        else if (WIFSTOPPED(waitStatus))
        {
            reason = ProcessState::stopped;
            info = WSTOPSIG(waitStatus);
        }
        else
        {
        }
    }

    std::unique_ptr<Process> Process::Launch(std::filesystem::path path, bool debug)
    {
        Pipe channel(true);
        pid_t pid;
        if ((pid = fork()) < 0)
        {
            Error::SendErrno("fork failed");
        }
        if (pid == 0)
        {
            channel.CloseRead();
            if (debug && ptrace(PTRACE_TRACEME, 0, nullptr, nullptr) < 0)
            {
                ExitWithPerror(channel, "Tracing failed");
            }
            if (execlp(path.c_str(), path.c_str(), nullptr) < 0)
            {
                ExitWithPerror(channel, "exec failed");
            }
        }
        channel.CloseWrite();
        auto data = channel.Read();
        channel.CloseRead();
        if (data.size() > 0)
        {
            waitpid(pid, nullptr, 0);
            auto chars = reinterpret_cast<char*>(data.data());
            Error::Send(std::string(chars, chars + data.size()));
        }


        std::unique_ptr<Process> process(new Process(pid, true, debug));
        if (debug)
        {
            process->WaitOnSignal();
        }
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

        std::unique_ptr<Process> process(new Process(pid, false, true));
        process->WaitOnSignal();
        return process;
    }

    Process::~Process()
    {
        if (pid != 0)
        {
            int status;
            if (isAttached)
            {
                if (state == ProcessState::running)
                {
                    // Stop the process(The process must be stopped to detach)
                    kill(pid, SIGSTOP);
                    waitpid(pid, &status, 0);
                }
                ptrace(PTRACE_DETACH, pid, nullptr, nullptr);
                kill(pid, SIGCONT);
            }
            if (terminateOnEnd)
            {
                kill(pid, SIGKILL);
                waitpid(pid, &status, 0);
            }
        }
    }

    void Process::Resume()
    {
        if (ptrace(PTRACE_CONT, pid, nullptr, nullptr) < 0)
        {
            Error::SendErrno("Could not resume");
        }
        state = ProcessState::running;
    }

    StopReason Process::WaitOnSignal()
    {
        int waitStatus;
        int options = 0;
        if (waitpid(pid, &waitStatus, options) < 0)
        {
            Error::SendErrno("waitpid failed");
        }
        StopReason reason(waitStatus);
        state = reason.reason;
        return reason;
    }

} // namespace ldb