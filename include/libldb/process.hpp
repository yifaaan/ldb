#pragma once

#include <filesystem>
#include <cstdint>


#include <libldb/registers.hpp>
#include <libldb/types.hpp>
#include <libldb/breakpoint_site.hpp>
#include <libldb/stoppoint_collection.hpp>

namespace ldb
{
	enum class ProcessState
	{
		stopped,
		running,
		exited,
		terminated,
	};

	/// <summary>
	/// the reason why the process stopped(exited, terminated, or just stopped) using in WaitOnSignal()
	/// </summary>
	struct StopReason
	{
		explicit StopReason(int waitStatus);

		ProcessState reason;
		/// <summary>
		/// information about the stop(return value of the exit or signal that caused a stop or termination)
		/// </summary>
		std::uint8_t info;
	};

	class Process
	{
	public:
		/// <summary>
		/// launch a process
		/// </summary>
		/// <param name="path">program path</param>
		/// <param name="debug">whether attch it or not</param>
		/// <returns></returns>
		static std::unique_ptr<Process> Launch(std::filesystem::path path, bool debug = true, std::optional<int> stdoutReplacement = std::nullopt);

		/// <summary>
		/// attach a existing process
		/// </summary>
		/// <param name="pid"></param>
		/// <returns></returns>
		static std::unique_ptr<Process> Attach(pid_t pid);

		Process() = delete;
		Process(const Process&) = delete;
		Process& operator=(const Process&) = delete;
		Process(Process&&) = delete;
		Process& operator=(Process&&) = delete;

		~Process();

		void Resume();

		/// <summary>
		/// wait the process to stop
		/// </summary>
		/// <returns>signal that occurred</returns>
		StopReason WaitOnSignal();

		pid_t Pid() const { return pid; }

		ProcessState State() const { return state; }

		VirtAddr GetPc() const
		{
			auto pc = GetRegisters().ReadByIdAs<std::uint64_t>(RegisterId::rip);
			return VirtAddr{ pc };
		}



		Registers& GetRegisters() { return *registers; }
		const Registers& GetRegisters() const { return *registers; }

		void WriteUserArea(std::size_t offset, std::uint64_t data);

		void WriteFprs(const user_fpregs_struct& fprs);
		void WriteGprs(const user_regs_struct& gprs);

		auto& CreateBreakpointSite(VirtAddr address);

		auto& BreakpointSites() { return breakpointSites; }
		const auto& BreakpointSites() const { return breakpointSites; }

	private:
		Process(pid_t _pid, bool _terminateOnEnd, bool _isAttached)
			: pid(_pid)
			, terminateOnEnd(_terminateOnEnd)
			, isAttached(_isAttached)
			, registers(new Registers(*this))
		{
		}

		void ReadAllRegisters();

	private:
		pid_t pid = 0;

		/// <summary>
		/// whether clean up the inferior process if we launched it ourselves or not
		/// </summary>
		bool terminateOnEnd = true;

		ProcessState state = ProcessState::stopped;

		/// <summary>
		/// whether debug the launched process or not
		/// </summary>
		bool isAttached = true;

		std::unique_ptr<Registers> registers;

		/// <summary>
		/// physical breakpoint sites
		/// </summary>
		StoppointCollection<BreakpointSite> breakpointSites;
	};
}