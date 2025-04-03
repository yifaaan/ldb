#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <libldb/process.hpp>
#include <libldb/error.hpp>
#include <libldb/pipe.hpp>


namespace
{
	/// <summary>
	/// write error message back to the parent process using the pipe, then exit
	/// </summary>
	/// <param name="channel"></param>
	/// <param name="prefix"></param>
	void ExitWithPerror(ldb::Pipe& channel, std::string_view prefix)
	{
		auto message = std::string(prefix) + ": " + std::strerror(errno);
		channel.Write(reinterpret_cast<std::byte*>(message.data()), message.size());
		std::exit(-1);
	}
}

namespace ldb
{
	std::unique_ptr<Process> Process::Launch(std::filesystem::path path)
	{
		// channle for son process to send error message back to the parent process
		Pipe channel(/*closeOnExec*/true);
		pid_t pid;
		if ((pid = fork()) < 0)
		{
			Error::SendErrno("fork failed");
		}

		if (pid == 0)
		{
			channel.CloseRead();
			if (ptrace(PTRACE_TRACEME, 0, nullptr, nullptr) < 0)
			{
				ExitWithPerror(channel, "Tracing failed");
			}
			// because of `PTRACE_TRACEME`, the child will stop before executing its main function
			if (execlp(path.c_str(), path.c_str(), nullptr) < 0)
			{
				ExitWithPerror(channel, "execlp failed");
			}
		}
		channel.CloseWrite();
		// once there is no error in the child process, the read() will return immediately because of [closeOnExec] and [exit]
		auto errMsg = channel.Read();
		channel.CloseRead();

		// error occured in the child process
		if (!errMsg.empty())
		{
			waitpid(pid, nullptr, 0);
			auto chars = reinterpret_cast<char*>(errMsg.data());
			Error::Send({ chars, errMsg.size() });
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