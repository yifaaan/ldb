#pragma once

#include <memory>
#include <filesystem>
#include <optional>

#include <sys/types.h>

#include <libldb/registers.hpp>
#include <libldb/types.hpp>
#include <libldb/breakpoint_site.hpp>
#include <libldb/stoppoint_collection.hpp>
#include <libldb/watchpoint.hpp>

namespace ldb
{
    enum class ProcessState
    {
        Stopped,
        Running,
        Exited,
        Terminated,
    };

    enum class TrapType
    {
        SingleStep,
        SoftwareBreak,
        HardwareBreak,
        Syscall,
        Unknown,
    };

    struct SyscallInformation
    {
        std::uint16_t id;
        bool entry;
        union
        {
            std::array<std::uint64_t, 6> args;
            std::int64_t ret;
        };
    };

    struct StopReason
    {
        StopReason(int waitStatus);

        ProcessState reason;
        std::uint8_t info;
        std::optional<TrapType> trapReason;
        std::optional<SyscallInformation> syscallInfo;
    };



    class SyscallCatchPolicy
    {
    public:
        enum Mode
        {
            None,
            Some,
            All,
        };

        static SyscallCatchPolicy CatchAll()
        {
            return { Mode::All, {} };
        }

        static SyscallCatchPolicy CatchNone()
        {
            return { Mode::None, {} };
        }

        static SyscallCatchPolicy CatchSome(std::vector<int> _toCatch)
        {
            return { Mode::Some, _toCatch };
        }

        Mode GetMode() const
        {
            return mode;
        }

        const std::vector<int>& GetToCatch() const
        {
            return toCatch;
        }

    private:
        SyscallCatchPolicy(Mode _mode, std::vector<int> _toCatch)
            :mode{_mode}
            ,toCatch{std::move(_toCatch)}
        {}

        Mode mode = Mode::None;
        std::vector<int> toCatch;
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
        void AugmentStopReason(StopReason& reason);

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

        BreakpointSite& CreateBreakpointSite(VirtAddr address, bool hardware = false, bool internal = false);
        StoppointCollection<BreakpointSite>& BreakPointSites() { return breakpointSites; }
        const StoppointCollection<BreakpointSite>& BreakPointSites() const { return breakpointSites; }

        Watchpoint& CreateWatchpoint(VirtAddr address, StoppointMode mode, std::size_t size);
        StoppointCollection<Watchpoint>& Watchpoints() { return watchpoints; }
        const StoppointCollection<Watchpoint>& Watchpoinst() const { return watchpoints; }

        ldb::StopReason StepInstruction();

        std::vector<std::byte> ReadMemory(VirtAddr address, std::size_t amount) const;
        std::vector<std::byte> ReadMemoryWithoutTraps(VirtAddr address, std::size_t amount) const;
        void WriteMemory(VirtAddr address, Span<const std::byte> data);
        template<typename T>
        T ReadMemoryAs(VirtAddr address) const
        {
            auto data = ReadMemory(address, sizeof(T));
            return FromBytes<T>(data.data());
        }

        int SetHardwareBreakpoint(BreakpointSite::IdType id, VirtAddr address);
        void ClearHardwareStoppoint(int index);

        int SetWatchpoint(Watchpoint::IdType id, VirtAddr address, StoppointMode mode, std::size_t size);

        std::variant<BreakpointSite::IdType, Watchpoint::IdType> GetCurrentHardwareStoppoint() const;

        void SetSyscallCatchPolicy(SyscallCatchPolicy info)
        {
            syscallCatchPolicy = std::move(info);
        }
    private:
        Process(pid_t _pid, bool _terminateOnEnd, bool _isAttached)
                :pid(_pid)
                ,terminateOnEnd(_terminateOnEnd)
                ,isAttached(_isAttached)
                ,registers(new Registers(*this))
        {}

        void ReadAllRegisters();

        int SetHardwareBreakpoint(VirtAddr address, StoppointMode mode, std::size_t size);

        ldb::StopReason MaybeResumeFromSyscall(const StopReason& reason);

    private:
        pid_t pid = 0;
        /// clean up the inferior process
        bool terminateOnEnd = true;
        ProcessState state = ProcessState::Stopped;
        bool isAttached = true;
        std::unique_ptr<Registers> registers;
        StoppointCollection<BreakpointSite> breakpointSites;
        StoppointCollection<Watchpoint> watchpoints;
        SyscallCatchPolicy syscallCatchPolicy = SyscallCatchPolicy::CatchNone();
        bool expectingSyscallExit = false;
    };
}