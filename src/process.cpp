
#include <csignal>
#include <unistd.h>
#include <sys/user.h>
#include <sys/personality.h>
#include <sys/uio.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fstream>

#include <libldb/process.hpp>
#include <libldb/error.hpp>
#include <libldb/pipe.hpp>
#include <libldb/bit.hpp>
#include <libldb/types.hpp>
#include <libldb/elf.hpp>

#include <fmt/format.h>

namespace 
{
    /// writes a representation of errno to a pipe
    void ExitWithPerror(ldb::Pipe& channel, const std::string& prefix)
    {
        auto message = prefix + std::string(": ") + std::strerror(errno);
        channel.Write(reinterpret_cast<std::byte*>(message.data()), message.size());
        exit(-1);
    }

    std::uint64_t EncodeHardwareStoppointMode(ldb::StoppointMode mode)
    {
        switch (mode)
        {
        case ldb::StoppointMode::Write: return 0b01;
        case ldb::StoppointMode::ReadWrite: return 0b11;
        case ldb::StoppointMode::Execute: return 0b00;
        default:
            ldb::Error::Send("Invalid stoppoint mode");
        }
    }

    std::uint64_t EncodeHardwareStoppointSize(std::size_t size)
    {
        switch (size)
        {
        case 1: return 0b00;
        case 2: return 0b01;
        case 4: return 0b11;
        case 8: return 0b10;
        default: ldb::Error::Send("Invalid stoppoint size");
        }
    }

    int FindFreeStoppointRegister(std::uint64_t controlRegister)
    {
        for (int i = 0; i < 4; i++)
        {
            if ((controlRegister & (0b11 << (i * 2))) == 0)
            {
                return i;
            }
        }
        ldb::Error::Send("No remaining hardware debug registers");
    }

    void SetPtraceOptions(pid_t pid)
    {
        if (ptrace(PTRACE_SETOPTIONS, pid, nullptr, PTRACE_O_TRACESYSGOOD) < 0)
        {
            ldb::Error::SendErrno("Failed to set PTRACE_O_TRACESYSGOOD option");
        }
    }
}

ldb::StopReason::StopReason(int waitStatus)
{
    if (WIFEXITED(waitStatus))
    {
        reason = ProcessState::Exited;
        info = WEXITSTATUS(waitStatus);
    }
    else if (WIFSIGNALED(waitStatus))
    {
        reason = ProcessState::Terminated;
        info = WTERMSIG(waitStatus);
    }
    else if (WIFSTOPPED(waitStatus))
    {
        reason = ProcessState::Stopped;
        info = WSTOPSIG(waitStatus);
    }
}


std::unique_ptr<ldb::Process> ldb::Process::Launch(std::filesystem::path path, bool debug, std::optional<int> stdoutReplacement)
{
    Pipe channel(true);
    pid_t pid;
    
    if ((pid = fork()) < 0)
    {
        Error::SendErrno("fork failed");
    }

    if (pid == 0)
    {
        // set the pgid to the same as the child pid
        if (setpgid(0, 0) < 0)
        {
            ExitWithPerror(channel, "Could not set pgid");
        }

        personality(ADDR_NO_RANDOMIZE);
        // in child
        channel.CloseRead();

        if (stdoutReplacement)
        {
            if (dup2(*stdoutReplacement, STDOUT_FILENO) < 0)
            {
                ExitWithPerror(channel, "stdout replacement failed");
            }
        }
        // set itself up to be traced
        if (debug and ptrace(PTRACE_TRACEME, 0, nullptr, nullptr) < 0)
        {
            ExitWithPerror(channel, "Tracing failed");
        }
        // kernal will stop the process on a call to exec if it's being traced using ptrace
        if (execlp(path.c_str(), path.c_str(), nullptr) < 0)
        {
            ExitWithPerror(channel, "exec failed");
        }
    }


    // in parent
    channel.CloseWrite();
    auto data = channel.Read();
    channel.CloseRead();

    if (data.size() > 0)
    {
        waitpid(pid, nullptr, 0);
        auto chars = reinterpret_cast<char*>(data.data());
        Error::Send(std::string(chars, chars + data.size()));
    }

    std::unique_ptr<Process> childProc(new Process(pid, true, debug));
    if (debug)
    {
        childProc->WaitOnSignal();
        SetPtraceOptions(childProc->Pid());
    }
    return childProc;
}

std::unique_ptr<ldb::Process> ldb::Process::Attach(pid_t pid)
{
    if (pid == 0)
    {
        Error::Send("Invalid PID");
    }
    if (ptrace(PTRACE_ATTACH, pid, nullptr, nullptr) < 0)
    {
        Error::SendErrno("Could not attach");
    }

    std::unique_ptr<Process> beAttachedProc(new Process(pid, false, true));
    beAttachedProc->WaitOnSignal();
    SetPtraceOptions(beAttachedProc->Pid());
    return beAttachedProc;
}

ldb::Process::~Process()
{
    if (pid != 0)
    {
        int status;
        if (isAttached)
        {
            if (state == ProcessState::Running)
            {
                kill(pid, SIGSTOP);
                waitpid(pid, &status, 0);
            }

            ptrace(PTRACE_DETACH, pid, nullptr, nullptr);
            kill(pid, SIGCONT);
        }

        if (terminateOnEnd)
        {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
        }
    }
}

void ldb::Process::Resume()
{
    auto pc = GetPc();
    if (breakpointSites.EnabledStoppointAtAddress(pc))
    {
        auto& bp = breakpointSites.GetByAddress(pc);
        bp.Disable();

        if (ptrace(PTRACE_SINGLESTEP, pid, nullptr, nullptr) < 0)
        {
            Error::SendErrno("Failed to single step");
        }
        int waitStatus;
        if (waitpid(pid, &waitStatus, 0) < 0)
        {
            Error::SendErrno("waitpid failed");
        }
        bp.Enable();
    }

    auto request = syscallCatchPolicy.GetMode() == SyscallCatchPolicy::Mode::None ? PTRACE_CONT : PTRACE_SYSCALL;
    if (ptrace(request, pid, nullptr, nullptr) < 0)
    {
        Error::SendErrno("Could not resume");
    }
    state = ProcessState::Running;
}

ldb::StopReason ldb::Process::WaitOnSignal()
{
    int waitStatus;
    int options = 0;
    if (waitpid(pid, &waitStatus, options) < 0)
    {
        Error::SendErrno("waitpid failed");
    }
    StopReason reason(waitStatus);
    // update traced process's state
    state = reason.reason;

    if (isAttached and state == ProcessState::Stopped)
    {
        ReadAllRegisters();
        AugmentStopReason(reason);
        // get address of int3
        auto instrBegin = GetPc() - 1;
        if (reason.info == SIGTRAP)
        {
            if (
                reason.trapReason == TrapType::SoftwareBreak
                and breakpointSites.ContainsAddress(instrBegin) 
                and breakpointSites.GetByAddress(instrBegin).IsEnabled())
            {
                SetPc(instrBegin);
            }
            else if (reason.trapReason == TrapType::HardwareBreak)
            {
                auto id = GetCurrentHardwareStoppoint();
                if (id.index() == 1)
                {
                    watchpoints.GetById(std::get<1>(id)).UpdateData();
                }
            }
            else if (reason.trapReason == TrapType::Syscall)
            {
                reason = MaybeResumeFromSyscall(reason);
            }
        }
    }
    return reason;
}

void ldb::Process::AugmentStopReason(StopReason& reason)
{
    siginfo_t info;
    if (ptrace(PTRACE_GETSIGINFO, pid, nullptr, &info) < 0)
    {
        Error::SendErrno("Failed to get signal info");
    }

    // whether stopped due to a syscall
    if (reason.info == (SIGTRAP | 0x80))
    {
        auto& sysInfo = reason.syscallInfo.emplace();
        auto& regs = GetRegisters();

        if (expectingSyscallExit)
        {
            sysInfo.entry = false;
            sysInfo.id = regs.ReadByIdAs<std::uint64_t>(RegisterId::orig_rax);
            sysInfo.ret = regs.ReadByIdAs<std::uint64_t>(RegisterId::rax);
            expectingSyscallExit = false;
        }
        else
        {
            sysInfo.entry = true;
            sysInfo.id = regs.ReadByIdAs<std::uint64_t>(RegisterId::orig_rax);

            std::array<RegisterId, 6> argRegs =
            {
                RegisterId::rdi,
                RegisterId::rsi,
                RegisterId::rdx,
                RegisterId::r10,
                RegisterId::r8,
                RegisterId::r9,
            };

            for (int i = 0; i < 6; i++)
            {
                sysInfo.args[i] = regs.ReadByIdAs<std::uint64_t>(argRegs[i]);
            }
            expectingSyscallExit = true;
        }

        reason.info = SIGTRAP;
        reason.trapReason = TrapType::Syscall;
        return;
    }

    expectingSyscallExit = false;

    reason.trapReason = TrapType::Unknown;
    if (reason.info == SIGTRAP)
    {
        switch (info.si_code)
        {
        case TRAP_TRACE:
            reason.trapReason = TrapType::SingleStep;
            break;
        case SI_KERNEL:
            reason.trapReason = TrapType::SoftwareBreak;
            break;
        case TRAP_HWBKPT:
            reason.trapReason = TrapType::HardwareBreak;
            break;
        }
    }
}

void ldb::Process::ReadAllRegisters()
{
    // read gpr
    if (ptrace(PTRACE_GETREGS, pid, nullptr, &GetRegisters().data.regs) < 0)
    {
        Error::SendErrno("Could not read GPR registers");
    }
    // read fpr
    if (ptrace(PTRACE_GETFPREGS, pid, nullptr, &GetRegisters().data.i387) < 0)
    {
        Error::SendErrno("Could not read FPR registers");
    }
    // read dbr
    for (int i = 0; i < 8; i++)
    {
        auto id = static_cast<int>(RegisterId::dr0) + i;
        auto info = RegisterInfoById(static_cast<RegisterId>(id));

        errno = 0;
        std::int64_t data = ptrace(PTRACE_PEEKUSER, pid, info.offset, nullptr);
        if (errno != 0)
        {
            Error::SendErrno("Could not read debug register");
        }

        GetRegisters().data.u_debugreg[i] = data;
    }

}

void ldb::Process::WriteUserArea(std::size_t offset, std::uint64_t data)
{
    if (ptrace(PTRACE_POKEUSER, pid, offset, data) < 0)
    {
        Error::SendErrno("Could not write to user area");
    }
}

void ldb::Process::WriteFprs(const user_fpregs_struct& fprs)
{
    if (ptrace(PTRACE_SETFPREGS, pid, nullptr, &fprs) < 0)
    {
        Error::SendErrno("Could not write floating point registers");
    }
}

void ldb::Process::WriteGprs(const user_regs_struct& gprs)
{
    if (ptrace(PTRACE_SETREGS, pid, nullptr, &gprs) < 0)
    {
        Error::SendErrno("Could not write general purpose registers");
    }
}

ldb::BreakpointSite& ldb::Process::CreateBreakpointSite(VirtAddr address, bool hardware, bool internal)
{
    if (breakpointSites.ContainsAddress(address))
    {
        Error::Send("Breakpoint site already created at address " + std::to_string(address.Addr()));
    }
    return breakpointSites.Push(std::unique_ptr<BreakpointSite>(new BreakpointSite(*this, address, hardware, internal)));
}

ldb::Watchpoint& ldb::Process::CreateWatchpoint(VirtAddr address, StoppointMode mode, std::size_t size)
{
    if (watchpoints.ContainsAddress(address))
    {
        Error::Send("Watchpoint site already created at address " + std::to_string(address.Addr()));
    }
    return watchpoints.Push(std::unique_ptr<Watchpoint>(new Watchpoint(*this, address, mode, size)));
}

ldb::StopReason ldb::Process::StepInstruction()
{
    std::optional<BreakpointSite*> toReenable;
    auto pc = GetPc();
    if (breakpointSites.EnabledStoppointAtAddress(pc))
    {
        auto& bp = breakpointSites.GetByAddress(pc);
        bp.Disable();
        toReenable = &bp;
    }

    if (ptrace(PTRACE_SINGLESTEP, pid, nullptr, nullptr) < 0)
    {
        Error::SendErrno("Could not single step");
    }
    auto reason = WaitOnSignal();

    if (toReenable)
    {
        toReenable.value()->Enable();
    }
    return reason;
}

std::vector<std::byte> ldb::Process::ReadMemory(VirtAddr address, std::size_t amount) const
{
    std::vector<std::byte> ret(amount);

    iovec localDesc{ret.data(), ret.size()};
    std::vector<iovec> remoteDescs;

    while (amount > 0)
    {
        // page offset: address.Addr() & 0xfff
        auto upToNextPage = 0x1000 - (address.Addr() & 0xfff);
        auto chunkSize = std::min(amount, upToNextPage);
        remoteDescs.push_back({reinterpret_cast<void*>(address.Addr()), chunkSize});
        amount -= chunkSize;
        address += chunkSize;
    }

    if (process_vm_readv(pid, &localDesc, 1, remoteDescs.data(), remoteDescs.size(), 0) < 0)
    {
        Error::SendErrno("Could not read process memory");
    }
    return ret;
}

std::vector<std::byte> ldb::Process::ReadMemoryWithoutTraps(VirtAddr address, std::size_t amount) const
{
    auto memory = ReadMemory(address, amount);
    auto sites = breakpointSites.GetInRegion(address, address + amount);

    for (auto site : sites)
    {
        if (!site->IsEnabled() or site->IsHardware()) continue;
        auto offset = site->Address() - address.Addr();
        memory[offset.Addr()] = site->savedData;
    }
    return memory;
}

void ldb::Process::WriteMemory(VirtAddr address, Span<const std::byte> data)
{
    std::size_t written = 0;
    while (written < data.Size())
    {
        auto remaining = data.Size() - written;
        std::uint64_t word;
        if (remaining >= 8)
        {
            word = FromBytes<std::uint64_t>(data.Begin() + written);
        }
        else
        {
            auto read = ReadMemory(address + written, 8);
            auto wordData = reinterpret_cast<char*>(&word);
            std::memcpy(wordData, data.Begin() + written, remaining);
            std::memcpy(wordData + remaining, read.data() + remaining, 8 - remaining);
        }
        if (ptrace(PTRACE_POKEDATA, pid, address + written, word) < 0)
        {
            Error::SendErrno("Failed to write memory");
        }
        written += 8;
    }
}

int ldb::Process::SetHardwareBreakpoint(BreakpointSite::IdType id, VirtAddr address)
{
    return SetHardwareBreakpoint(address, StoppointMode::Execute, 1);
}

int ldb::Process::SetHardwareBreakpoint(VirtAddr address, StoppointMode mode, std::size_t size)
{
    auto& regs = GetRegisters();
    auto control = regs.ReadByIdAs<std::uint64_t>(RegisterId::dr7);

    // four dbg registers idx: 0, 1, 2, 3
    int freeSpace = FindFreeStoppointRegister(control);
    auto id = static_cast<int>(RegisterId::dr0) + freeSpace;
    regs.WriteById(static_cast<RegisterId>(id), address.Addr());


    // 0
    // Local DR0 breakpoint enabled
    // 1
    // Global DR0 breakpoint enabled
    // 2
    // Local DR1 breakpoint enabled
    // 3
    // Global DR1 breakpoint enabled
    // 4
    // Local DR2 breakpoint enabled
    // 5
    // Global DR2 breakpoint enabled
    // 6
    // Local DR3 breakpoint enabled
    // 7
    // Global DR3 breakpoint enabled
    // 8-15
    // Reserved/not relevant to us
    // 16-17
    // Conditions for DR0
    // 18-19
    // Byte size of DR0 breakpoint
    // 20–21
    // Conditions for DR1
    // 22–23
    // Byte size of DR1 breakpoint
    // 24–25
    // Conditions for DR2
    // 26–27
    // Byte size of DR2 breakpoint
    // 28–29
    // Conditions for DR3
    // 30–31
    // Byte size of DR3 breakpoint

    // condition bits
    // 00b
    // Instruction execution only
    // 01b
    // Data writes only
    // 10b
    // I/O reads and writes (generally unsupported)
    // 11b
    // Data reads and writes

    // size bits
    // 00b
    // One byte
    // 01b
    // Two bytes
    // 10b
    // Eight bytes
    // 11b
    // Four bytes

    // Enable bit location: a = free_space * 2
    // Mode bits location: b = free_space * 4 + 16
    // Size bits location: c = free_space * 4 + 18

    // for example, set a read/write stop point of size 8 on debug register 2.
    // a = 4, b = 24, c = 26
    // enable flag  = 00000000 00000000 00000000 00010000
    // mode flag    = 00000011 00000000 00000000 00000000 : Data reads and writes
    // size flag    = 00001000 00000000 00000000 00000000 : Eight bytes
    // or flags     = 00001011 00000000 00000000 00010000
    auto modeFlag = EncodeHardwareStoppointMode(mode);
    auto sizeFlag = EncodeHardwareStoppointSize(size);

    auto enableBit = (1 << (freeSpace * 2));
    auto modeBits = (modeFlag << (freeSpace * 4 + 16));
    auto sizeBits = (sizeFlag << (freeSpace * 4 + 18));

    auto clearMask = (0b11 << (freeSpace * 2)) | (0b1111 << (freeSpace * 4 + 16));
    
    // clear all old informations of this dbg register, keep the other informations
    auto masked = control & ~clearMask;
    masked |= enableBit | modeBits | sizeBits;

    regs.WriteById(RegisterId::dr7, masked);
    return freeSpace;
}

void ldb::Process::ClearHardwareStoppoint(int index)
{
    auto id = static_cast<int>(RegisterId::dr0) + index;
    GetRegisters().WriteById(static_cast<RegisterId>(id), 0);

    auto control = GetRegisters().ReadByIdAs<std::uint64_t>(RegisterId::dr7);

    auto clearMask = (0b11 << (index * 2)) | (0b1111 << (index * 4 + 16));
    auto masked = control & ~clearMask;

    GetRegisters().WriteById(RegisterId::dr7, masked);
}

int ldb::Process::SetWatchpoint(Watchpoint::IdType id, VirtAddr address, StoppointMode mode, std::size_t size)
{
    return SetHardwareBreakpoint(address, mode, size);
}

std::variant<ldb::BreakpointSite::IdType, ldb::Watchpoint::IdType> ldb::Process::GetCurrentHardwareStoppoint() const
{
    auto& regs = GetRegisters();
    auto status = regs.ReadByIdAs<std::uint64_t>(RegisterId::dr6);
    auto index = __builtin_ctzll(status);

    auto id = static_cast<int>(RegisterId::dr0) + index;
    auto addr = VirtAddr(regs.ReadByIdAs<std::uint64_t>(static_cast<RegisterId>(id)));

    using RetType = std::variant<ldb::BreakpointSite::IdType, ldb::Watchpoint::IdType>;
    if (breakpointSites.ContainsAddress(addr))
    {
        auto siteId = breakpointSites.GetByAddress(addr).Id();
        return RetType{std::in_place_index<0>, siteId};
    }
    else
    {
        auto watchId = watchpoints.GetByAddress(addr).Id();
        return RetType{std::in_place_index<1>, watchId};
    }
}

ldb::StopReason ldb::Process::MaybeResumeFromSyscall(const StopReason& reason)
{
    if (syscallCatchPolicy.GetMode() == SyscallCatchPolicy::Mode::Some)
    {
        auto& toCatch = syscallCatchPolicy.GetToCatch();
        if (auto it = std::find(std::begin(toCatch), std::end(toCatch), reason.syscallInfo->id); it == std::end(toCatch))
        {
            Resume();
            return WaitOnSignal();
        }
    }
    return reason;
}

std::unordered_map<int, std::uint64_t> ldb::Process::GetAuxv() const
{
    auto path = fmt::format("/proc/{}/auxv", pid);
    std::ifstream auxv{ path };

    std::unordered_map<int, std::uint64_t> ret;
    std::uint64_t id, value;

    auto read = [&](auto& info)
    {
        auxv.read(reinterpret_cast<char*>(&info), sizeof(info));
    };

    for (read(id); id != AT_NULL; read(id))
    {
        read(value);
        ret[id] = value;
    }
    return ret;
}