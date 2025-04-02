#pragma once

#include <sys/types.h>

#include <filesystem>
#include <memory>
#include <cstdint>

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
		static std::unique_ptr<Process> Launch(std::filesystem::path path);
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

	private:
		Process(pid_t _pid, bool _terminateOnEnd)
			: pid(_pid)
			, terminateOnEnd(_terminateOnEnd)
		{
		}

	private:
		pid_t pid = 0;

		/// <summary>
		/// whether clean up the inferior process if we launched it ourselves or not
		/// </summary>
		bool terminateOnEnd = true;

		ProcessState state = ProcessState::stopped;
	};
}