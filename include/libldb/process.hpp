#pragma once

#include <sys/types.h>

#include <filesystem>
#include <memory>
#include <cstdint>


#include <libldb/registers.hpp>

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
		static std::unique_ptr<Process> Launch(std::filesystem::path path, bool debug = true);

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



		Registers& GetRegisters() { return *registers; }
		const Registers& GetRegisters() const { return *registers; }

		void WriteUserArea(std::size_t offset, std::uint64_t data);

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
	};
}