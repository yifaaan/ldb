#ifndef LDB_PROCESS_HPP
#define LDB_PROCESS_HPP

#include <memory>
#include <filesystem>
#include <sys/types.h>

namespace ldb
{
    enum class ProcessState
    {
        Stopped,
        Running,
        Exited,
        Terminated,
    };

    class Process
    {
    public:
        Process() = delete;
        Process(const Process&) = delete;
        Process& operator=(const Process&) = delete;

        ~Process();

        static std::unique_ptr<Process> Launch(std::filesystem::path path);

        static std::unique_ptr<Process> Attach(pid_t pid);

        void Resume();

        void WaitOnSignal();

        pid_t Pid() const { return pid; }

        ProcessState State() const { return state; }
    
    private:
        Process(pid_t _pid, bool _terminateOnEnd)
                :pid(_pid)
                ,terminateOnEnd(_terminateOnEnd)
        {}

    private:
        pid_t pid = 0;
        /// clean up the inferior process
        bool terminateOnEnd = true;
        ProcessState state = ProcessState::Stopped;
    };
}

#endif