#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <libldb/process.hpp>
#include <libldb/error.hpp>
#include <libldb/pipe.hpp>
#include <libldb/registers.hpp>

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
	std::unique_ptr<Process> Process::Launch(std::filesystem::path path, bool debug, std::optional<int> stdoutReplacement)
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
			if (stdoutReplacement)
			{
				if (dup2(*stdoutReplacement, STDOUT_FILENO) < 0)
				{
					ExitWithPerror(channel, "stdout replacement failed");
				}
			}
			if (debug && ptrace(PTRACE_TRACEME, 0, nullptr, nullptr) < 0)
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

		std::unique_ptr<Process> process{ new Process{pid, /*terminateOnEnd*/true, debug} };
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

		std::unique_ptr<Process> process{ new Process{pid, /*terminateOnEnd*/false, /*isAttached*/true} };
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
				// inferior must be stopped before cal PTRACE_DETACH
				if (state == ProcessState::running)
				{
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
			Error::SendErrno("waitpit failed");
		}
		StopReason reason(waitStatus);
		state = reason.reason;
		if (isAttached && state == ProcessState::stopped)
		{
			ReadAllRegisters();
		}
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

	void Process::ReadAllRegisters()
	{
		if (ptrace(PTRACE_GETREGS, pid, nullptr, &GetRegisters().data.regs) < 0)
		{
			Error::SendErrno("Could not read GPR registers");
		}
		if (ptrace(PTRACE_GETFPREGS, pid, nullptr, &GetRegisters().data.i387) < 0)
		{
			Error::SendErrno("Could not read FPR registers");
		}
		for (int i = 0; i < 8; i++)
		{
			// read debug registers
			auto id = static_cast<int>(RegisterId::dr0) + i;
			auto info = RegisterInfoById(static_cast<RegisterId>(id));
			errno = 0;
			std::int64_t data = ptrace(PTRACE_PEEKUSER, pid, info.offset, nullptr);
			if (errno != 0) Error::SendErrno("Could not read debug register");
			GetRegisters().data.u_debugreg[i] = data;
		}
	}

	void Process::WriteUserArea(std::size_t offset, std::uint64_t data)
	{
		if (ptrace(PTRACE_POKEUSER, pid, offset, data))
		{
			Error::SendErrno("Could not write to user area");
		}
	}

	void Process::WriteFprs(const user_fpregs_struct& fprs)
	{
		if (ptrace(PTRACE_SETFPREGS, pid, nullptr, &fprs) < 0)
		{
			Error::SendErrno("Could not write floating point registers");
		}
	}

	void Process::WriteGprs(const user_regs_struct& gprs)
	{
		if (ptrace(PTRACE_SETREGS, pid, nullptr, &gprs) < 0)
		{
			Error::SendErrno("Could not write general purpose registers");
		}
	}
}