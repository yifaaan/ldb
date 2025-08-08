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

    struct StopReason
    {
        StopReason(int waitStatus);

        ProcessState reason;
        std::uint8_t info;
    };

    class Process
    {
    public:
        Process() = delete;
        Process(const Process&) = delete;
        Process& operator=(const Process&) = delete;
        ~Process();

        static std::unique_ptr<Process> Launch(std::filesystem::path path, bool debug = true);
        static std::unique_ptr<Process> Attach(pid_t pid);

        void Resume();
        StopReason WaitOnSignal();

        ProcessState State() const
        {
            return state;
        }

        pid_t Pid() const
        {
            return pid;
        }

    private:
        Process(pid_t _pid, bool _terminateOnEnd, bool _isAttached) : pid(_pid), terminateOnEnd(_terminateOnEnd), isAttached(_isAttached)
        {
        }

        pid_t pid = 0;
        bool terminateOnEnd = true;
        ProcessState state = ProcessState::stopped;
        bool isAttached = true;
    };
} // namespace ldb