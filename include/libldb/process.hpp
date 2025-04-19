#pragma once

#include <csignal>

#include <filesystem>
#include <cstdint>
#include <unordered_map>

#include <libldb/registers.hpp>
#include <libldb/types.hpp>
#include <libldb/breakpoint_site.hpp>
#include <libldb/stoppoint_collection.hpp>
#include <libldb/watchpoint.hpp>


namespace ldb
{
	enum class ProcessState
	{
		stopped,
		running,
		exited,
		terminated,
	};

	struct SyscallInformation
	{
		std::uint16_t id;
		bool entry;
		union
		{
			std::array<std::uint64_t, 6> args;
			std::uint64_t ret;
		};
	};

	/// <summary>
	/// whether a SIGTRAP occurred due to these reasons
	/// </summary>
	enum class TrapType
	{
		singleStep,
		softwareBreak,
		hardwareBreak,
		syscall,
		unknown,
	};

	/// <summary>
	/// the reason why the process stopped(exited, terminated, or just stopped) using in WaitOnSignal()
	/// </summary>
	struct StopReason
	{
		StopReason() = default;
		explicit StopReason(int waitStatus);

		StopReason(ProcessState _reason,
			std::uint8_t _info,
			std::optional<TrapType> _trapReason = std::nullopt,
			std::optional<SyscallInformation> _syscallInfo = std::nullopt)
			: reason(_reason)
			, info(_info)
			, trapReason(_trapReason)
			, syscallInfo(_syscallInfo)
		{}

		bool IsStep() const
		{
			return reason == ProcessState::stopped && info == SIGTRAP && trapReason == TrapType::singleStep;
		}

		bool IsBreakpoint() const
		{
			return reason == ProcessState::stopped && info == SIGTRAP && (trapReason == TrapType::softwareBreak || trapReason == TrapType::hardwareBreak);
		}



		ProcessState reason;
		/// <summary>
		/// information about the stop(return value of the exit or signal that caused a stop or termination)
		/// </summary>
		std::uint8_t info;

		/// <summary>
		/// stop occurred due to SIGTRAP
		/// </summary>
		std::optional<TrapType> trapReason;

		std::optional<SyscallInformation> syscallInfo;
	};

	class SyscallCatchPolicy
	{
	public:
		enum Mode
		{
			none, some, all,
		};

		static SyscallCatchPolicy CatchAll()
		{
			return { Mode::all, {} };
		}
		static SyscallCatchPolicy CatchNone()
		{
			return { Mode::none, {} };
		}
		static SyscallCatchPolicy CatchSome(std::vector<int> toCatch)
		{
			return { Mode::some, std::move(toCatch) };
		}

		Mode GetMode() const { return mode; }
		const std::vector<int>& GetToCatch() const { return toCatch; }

	private:
		SyscallCatchPolicy(Mode _mode, std::vector<int> _toCatch)
			: mode(_mode)
			, toCatch(_toCatch)
		{ }

		Mode mode = Mode::none;
		std::vector<int> toCatch;
	};

	
	class Target;
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

		StopReason StepInstruction();

		/// <summary>
		/// wait the process to stop
		/// </summary>
		/// <returns>signal that occurred</returns>
		StopReason WaitOnSignal();

		void AugmentStopReason(StopReason& reason);

		pid_t Pid() const { return pid; }

		ProcessState State() const { return state; }

		VirtAddr GetPc() const
		{
			auto pc = GetRegisters().ReadByIdAs<std::uint64_t>(RegisterId::rip);
			return VirtAddr{ pc };
		}

		void SetPc(VirtAddr address)
		{
			GetRegisters().WriteById(RegisterId::rip, address.Addr());
		}

		std::vector<std::byte> ReadMemory(VirtAddr address, std::size_t amount) const;
		std::vector<std::byte> ReadMemoryWithoutTraps(VirtAddr address, std::size_t amount) const;
		void WriteMemory(VirtAddr address, Span<const std::byte> data);

		template <typename T>
		T ReadMemoryAs(VirtAddr address) const
		{
			auto data = ReadMemory(address, sizeof(T));
			return FromBytes<T>(data.data());
		}


		Registers& GetRegisters() { return *registers; }
		const Registers& GetRegisters() const { return *registers; }

		void WriteUserArea(std::size_t offset, std::uint64_t data);

		void WriteFprs(const user_fpregs_struct& fprs);
		void WriteGprs(const user_regs_struct& gprs);

		BreakpointSite& CreateBreakpointSite(VirtAddr address, bool hardware = false, bool internal = false);

		int SetHardwareBreakpoint(BreakpointSite::IdType id, VirtAddr address);
		void ClearHardwareStoppoint(int index);
		int SetWatchpoint(Watchpoint::IdType id, VirtAddr address, StoppointMode mode, std::size_t size);

		StoppointCollection<BreakpointSite>& BreakpointSites() { return breakpointSites; }
		const StoppointCollection<BreakpointSite>& BreakpointSites() const { return breakpointSites; }

		Watchpoint& CreateWatchpoint(VirtAddr address, StoppointMode mode, std::size_t size);
		StoppointCollection<Watchpoint>& Watchpoints() { return watchpoints; }
		const StoppointCollection<Watchpoint>& Watchpoints() const { return watchpoints; }

		std::variant<BreakpointSite::IdType, Watchpoint::IdType> GetCurrentHardwareStoppoint() const;

		void SetSyscallCatchPolicy(SyscallCatchPolicy policy)
		{
			syscallCatchPolicy = std::move(policy);
		}

		std::unordered_map<int, std::uint64_t> GetAuxv() const;

		void SetTarget(Target* _target)
		{
			target = _target;
		}

	private:
		Process(pid_t _pid, bool _terminateOnEnd, bool _isAttached)
			: pid(_pid)
			, terminateOnEnd(_terminateOnEnd)
			, isAttached(_isAttached)
			, registers(new Registers(*this))
		{
		}

		int SetHardwareStoppoint(VirtAddr address, StoppointMode mode, std::size_t size);

		void ReadAllRegisters();



	private:
		StopReason MaybeResumeFromSyscall(const StopReason& reason);

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

		StoppointCollection<Watchpoint> watchpoints;

		SyscallCatchPolicy syscallCatchPolicy = SyscallCatchPolicy::CatchNone();

		bool expectingSyscallExit = false;

		Target* target = nullptr;
	};
}