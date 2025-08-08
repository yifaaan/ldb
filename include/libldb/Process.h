#pragma once

#include <filesystem>
#include <memory>

namespace ldb
{
    enum class ProcessState
    {
        stopped,
        running,
        exited,
        terminated,
    };

    class Process
    {
    public:
        Process() = delete;
        Process(const Process&) = delete;
        Process& operator=(const Process&) = delete;

        static std::unique_ptr<Process> Launch(std::filesystem::path path);
        static std::unique_ptr<Process> Attach(pid_t pid);

        void Resume();
        void WaitOnSignal();

        ProcessState State() const
        {
            return state;
        }

        pid_t Pid() const
        {
            return pid;
        }

    private:
        Process(pid_t _pid, bool _terminateOnEnd) : pid(_pid), terminateOnEnd(_terminateOnEnd)
        {
        }

        pid_t pid = 0;
        bool terminateOnEnd = true;
        ProcessState state = ProcessState::stopped;
    };
} // namespace ldb