#pragma once

#include <memory>
#include <filesystem>
#include <optional>

#include <sys/types.h>

#include <libldb/registers.hpp>
#include <libldb/types.hpp>
#include <libldb/breakpoint_site.hpp>
#include <libldb/stoppoint_collection.hpp>

namespace ldb
{
    enum class ProcessState
    {
        Stopped,
        Running,
        Exited,
        Terminated,
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

        static std::unique_ptr<Process> Launch(std::filesystem::path path, bool debug = true, std::optional<int> stdoutReplacement = 1);

        static std::unique_ptr<Process> Attach(pid_t pid);

        void Resume();

        StopReason WaitOnSignal();

        pid_t Pid() const { return pid; }

        ProcessState State() const { return state; }

        Registers& GetRegisters() { return *registers; }
        const Registers& GetRegisters() const { return *registers; }

        void WriteUserArea(std::size_t offset, std::uint64_t data);

        void WriteFprs(const user_fpregs_struct& fprs);
        void WriteGprs(const user_regs_struct& gprs);

        VirtAddr GetPc() const 
        {
            return VirtAddr(GetRegisters().ReadByIdAs<std::uint64_t>(RegisterId::rip));
        }

        void SetPc(VirtAddr address)
        {
            GetRegisters().WriteById(RegisterId::rip, address.Addr());
        }

        BreakpointSite& CreateBreakpointSite(VirtAddr address);
        
        StoppointCollection<BreakpointSite>& BreakPointSites() { return breakpointSites; }
        const StoppointCollection<BreakpointSite>& BreakPointSites() const { return breakpointSites; }
    
        ldb::StopReason StepInstruction();
    private:
        Process(pid_t _pid, bool _terminateOnEnd, bool _isAttached)
                :pid(_pid)
                ,terminateOnEnd(_terminateOnEnd)
                ,isAttached(_isAttached)
                ,registers(new Registers(*this))
        {}

        void ReadAllRegisters();

    private:
        pid_t pid = 0;
        /// clean up the inferior process
        bool terminateOnEnd = true;
        ProcessState state = ProcessState::Stopped;
        bool isAttached = true;
        std::unique_ptr<Registers> registers;
        StoppointCollection<BreakpointSite> breakpointSites;
    };
}