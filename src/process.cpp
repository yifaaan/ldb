
#include <csignal>
#include <iostream>
#include <unistd.h>
#include <sys/user.h>
#include <sys/personality.h>
#include <sys/uio.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <libldb/process.hpp>
#include <libldb/error.hpp>
#include <libldb/pipe.hpp>
#include <libldb/bit.hpp>

namespace 
{
    /// writes a representation of errno to a pipe
    void ExitWithPerror(ldb::Pipe& channel, const std::string& prefix)
    {
        auto message = prefix + std::string(": ") + std::strerror(errno);
        channel.Write(reinterpret_cast<std::byte*>(message.data()), message.size());
        exit(-1);
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

    if (ptrace(PTRACE_CONT, pid, nullptr, nullptr) < 0)
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

        // get address of int3
        auto instrBegin = GetPc() - 1;
        if (reason.info == SIGTRAP and breakpointSites.EnabledStoppointAtAddress(instrBegin))
        {
            SetPc(instrBegin);
        }
    }
    return reason;
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

ldb::BreakpointSite& ldb::Process::CreateBreakpointSite(VirtAddr address)
{
    if (breakpointSites.ContainsAddress(address))
    {
        Error::Send("Breakpoint site already created at address " + std::to_string(address.Addr()));
    }
    return breakpointSites.Push(std::unique_ptr<BreakpointSite>(new BreakpointSite(*this, address)));
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
        if (!site->isEnabled()) continue;
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