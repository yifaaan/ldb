#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/personality.h>
#include <sys/uio.h>
#include <unistd.h>

#include <iostream>

#include <libldb/process.hpp>
#include <libldb/error.hpp>
#include <libldb/pipe.hpp>
#include <libldb/registers.hpp>
#include <libldb/bit.hpp>


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

	std::uint64_t EncodeHardwareStoppointMode(ldb::StoppointMode mode)
	{
		switch (mode)
		{
		case ldb::StoppointMode::write: return 0b01;
		case ldb::StoppointMode::readWrite: return 0b11;
		case ldb::StoppointMode::execute: return 0b00;
		default: ldb::Error::Send("Invalid stoppoint mode");
		}
	}

	std::uint64_t EncodeHardwareStoppointSize(std::size_t size)
	{
		switch (size)
		{
		case 1: return 0b00;
		case 2: return 0b01;
		case 4: return 0b11;
		case 8: return 0b10;
		default: ldb::Error::Send("Invalid stoppoint size");
		}
	}

	int FindFreeStoppointRegister(std::uint64_t controlRegister)
	{
		for (int i = 0; i < 4; i++)
		{
			if ((controlRegister & (0b11 << (i * 2))) == 0) return i;
		}
		ldb::Error::Send("No remaining hardware debug registers");
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
			personality(ADDR_NO_RANDOMIZE);
			channel.CloseRead();
			if (stdoutReplacement)
			{
				//close(STDOUT_FILENO);
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
		// for continue command
		auto pc = GetPc();
		if (breakpointSites.EnabledStoppointAtAddress(pc))
		{
			auto& bp = breakpointSites.GetByAddress(pc);
			bp.Disable();
			if (ptrace(PTRACE_SINGLESTEP, pid, nullptr, nullptr) < 0)
			{
				Error::SendErrno("Failed to single step");
			}
			int waitStatus;
			if (waitpid(pid, &waitStatus, 0) < 0)
			{
				Error::SendErrno("waitpid failed");
			}
			bp.Enable();
		}

		if (ptrace(PTRACE_CONT, pid, nullptr, nullptr) < 0)
		{
			Error::SendErrno("Could not resume");
		}
		state = ProcessState::running;
	}

	StopReason Process::StepInstruction()
	{
		std::optional<BreakpointSite*> toReenable;
		auto pc = GetPc();
		if (breakpointSites.EnabledStoppointAtAddress(pc))
		{
			auto& bp = breakpointSites.GetByAddress(pc);
			bp.Disable();
			toReenable = &bp;
		}
		if (ptrace(PTRACE_SINGLESTEP, pid, nullptr, nullptr) < 0)
		{
			Error::SendErrno("Could not single step");
		}
		auto reason = WaitOnSignal();
		if (toReenable)
		{
			toReenable.value()->Enable();
		}
		return reason;
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

			// for continue command
			auto instrBegin = GetPc() - 1;
			if (reason.info == SIGTRAP && breakpointSites.EnabledStoppointAtAddress(instrBegin))
			{
				SetPc(instrBegin);
			}
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

	std::vector<std::byte> Process::ReadMemory(VirtAddr address, std::size_t amount) const
	{
		std::vector<std::byte> ret(amount);
		iovec localDesc{ ret.data(), ret.size() };
		std::vector<iovec> remoteDescs;
		while (amount > 0)
		{
			auto upToNextPage = 0x1000 - (address.Addr() & 0xfff);
			auto chunkSize = std::min(amount, upToNextPage);
			remoteDescs.push_back({ reinterpret_cast<void*>(address.Addr()), chunkSize });
			amount -= chunkSize;
			address += chunkSize;
		}
		if (process_vm_readv(pid, &localDesc, 1, remoteDescs.data(), remoteDescs.size(), 0) < 0)
		{
			Error::SendErrno("Could not read process memory");
		}
		return ret;
	}

	std::vector<std::byte> Process::ReadMemoryWithoutTraps(VirtAddr address, std::size_t amount) const
	{
		auto memory = ReadMemory(address, amount);
		auto sites = breakpointSites.GetInRegion(address, address + amount);
		for (auto site : sites)
		{
			if (!site->IsEnabled() || site->IsHardware()) continue;
			auto offset = site->Address() - address.Addr();
			memory[offset.Addr()] = site->savedData;
		}
		return memory;
	}

	void Process::WriteMemory(VirtAddr address, Span<const std::byte> data)
	{
		std::size_t written = 0;
		while (written < data.Size())
		{
			auto remaining = data.Size() - written;
			//  ptrace can only write exactly eight bytes at a time
			std::uint64_t word;
			if (remaining >= 8)
			{
				word = FromBytes<std::uint64_t>(data.Begin() + written);
			}
			else
			{
				auto read = ReadMemory(address + written, 8);
				auto wordData = reinterpret_cast<char*>(&word);
				std::memcpy(wordData, data.Begin() + written, remaining);
				std::memcpy(wordData + remaining, read.data() + remaining, 8 - remaining);
			}
			if (ptrace(PTRACE_POKEDATA, pid, address + written, word) < 0)
			{
				Error::SendErrno("Failed to write memory");
			}
			written += 8;
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

	BreakpointSite& Process::CreateBreakpointSite(VirtAddr address, bool hardware, bool internal)
	{
		if (breakpointSites.ContainsAddress(address))
		{
			Error::Send("Breakpoint site already created at address " + std::to_string(address.Addr()));
		}
		return breakpointSites.Push(std::unique_ptr<BreakpointSite>{ new BreakpointSite(*this, address, hardware, internal) });
	}

	int Process::SetHardwareBreakpoint(BreakpointSite::IdType id, VirtAddr address)
	{
		return SetHardwareStoppoint(address, StoppointMode::execute, 1);
	}

	void Process::ClearHardwareStoppoint(int index)
	{
		auto id = static_cast<int>(RegisterId::dr0) + index;
		GetRegisters().WriteById(static_cast<RegisterId>(id), 0);
		auto control = GetRegisters().ReadByIdAs<std::uint64_t>(RegisterId::dr7);
		auto clearMask = (0b11 << (index * 2)) | (0b1111 << (index * 4 + 16));
		auto masked = control & ~clearMask;
		GetRegisters().WriteById(RegisterId::dr7, masked);
	}

	int Process::SetHardwareStoppoint(VirtAddr address, StoppointMode mode, std::size_t size)
	{
		auto& regs = GetRegisters();
		auto control = regs.ReadByIdAs<std::uint64_t>(RegisterId::dr7);
		int freeSpace = FindFreeStoppointRegister(control);
		auto id = static_cast<int>(RegisterId::dr0) + freeSpace;
		regs.WriteById(static_cast<RegisterId>(id), address.Addr());
		auto modeFlag = EncodeHardwareStoppointMode(mode);
		auto sizeFlag = EncodeHardwareStoppointSize(size);
		auto enableBit = 1 << (freeSpace * 2);
		auto modeBits = modeFlag << (freeSpace * 4 + 16);
		auto sizeBits = sizeFlag << (freeSpace * 4 + 18);
		auto clearMask = (0b11 << (freeSpace * 2)) | (0b111 << (freeSpace * 4 + 16));
		auto masked = control & ~clearMask;
		masked |= enableBit | modeBits | sizeBits;
		regs.WriteById(RegisterId::dr7, masked);
		return freeSpace;
	}
}