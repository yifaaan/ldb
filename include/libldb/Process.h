#pragma once

#include <filesystem>
#include <memory>
#include <optional>

#include <libldb/Registers.h>
#include <libldb/Types.h>
#include <libldb/BreakpointSite.h>
#include <libldb/StoppointCollection.h>

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

        static std::unique_ptr<Process> Launch(std::filesystem::path path, bool debug = true, std::optional<int> stdoutReplacement = std::nullopt);
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

        Registers& GetRegisters()
        {
            return *registers;
        }

        const Registers& GetRegisters() const
        {
            return *registers;
        }

        void WriteUserArea(size_t offset, uint64_t data);

        void WriteFPRs(const user_fpregs_struct& fpregs);
        void WriteGPRs(const user_regs_struct& regs);

        VirtAddr GetPC() const
        {
            return VirtAddr(GetRegisters().ReadByIdAs<uint64_t>(RegisterId::rip));
        }

        BreakpointSite& CreateBreakpointSite(VirtAddr addr);

        StoppointCollection<BreakpointSite>& BreakpointSites() { return breakpointSites; }
        const StoppointCollection<BreakpointSite>& BreakpointSites() const { return breakpointSites; }

    private:
        Process(pid_t _pid, bool _terminateOnEnd, bool _isAttached)
            : pid(_pid),
            terminateOnEnd(_terminateOnEnd),
            isAttached(_isAttached),
            registers(new Registers(*this))
        {
        }

        void ReadAllRegisters();

        pid_t pid = 0;
        bool terminateOnEnd = true;
        ProcessState state = ProcessState::stopped;
        bool isAttached = true;
        std::unique_ptr<Registers> registers;
        StoppointCollection<BreakpointSite> breakpointSites;
    };
} // namespace ldb