#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <libldb/process.hpp>
#include <libldb/error.hpp>

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
			// because of `PTRACE_TRACEME`, the child will stop before executing its main function
			if (execlp(path.c_str(), path.c_str(), nullptr) < 0)
			{
				Error::SendErrno("exec failed");
			}
		}

		std::unique_ptr<Process> process{ new Process{pid, /*terminateOnEnd*/true} };
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

		std::unique_ptr<Process> process{ new Process{pid, /*terminateOnEnd*/false} };
		process->WaitOnSignal();
		return process;
	}

	Process::~Process()
	{
		if (pid != 0)
		{
			int status;
			// inferior must be stopped before cal PTRACE_DETACH
			if (state == ProcessState::running)
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
			Error::SendErrno("waitpit failed");
		}
		StopReason reason(waitStatus);
		state = reason.reason;
		return reason;
	}

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
	}
}