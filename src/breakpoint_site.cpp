#include <cerrno>
#include <sys/ptrace.h>

#include <libldb/breakpoint_site.hpp>
#include <libldb/error.hpp>
#include <libldb/process.hpp>

namespace
{
    auto GetNextId()
    {
        static ldb::BreakpointSite::IdType id = 0;
        return ++id;
    }
}


ldb::BreakpointSite::BreakpointSite(Process& proc, VirtAddr _address)
        :process(&proc)
        ,address(_address)
        ,isEnable(false)
        ,savedData{}
{
    id = GetNextId();
}

void ldb::BreakpointSite::Enable()
{
    if (isEnable) return;

    errno = 0;
    std::uint64_t data = ptrace(PTRACE_PEEKDATA, process->Pid(), address, nullptr);
    if (errno != 0)
    {
        Error::SendErrno("Enabling breakpoint site failed: get data");
    }

    savedData = static_cast<std::byte>(data & 0xff);

    std::uint64_t int3 = 0xcc;
    //  replace the instruction at the given address with an int3 instruction
    std::uint64_t dataWithInt3 = ((data & ~0xff) | int3);

    if (ptrace(PTRACE_POKEDATA, process->Pid(), address, dataWithInt3) < 0)
    {
        Error::SendErrno("Enabling breakpoint site failed: replace data");
    }
    isEnable = true;
}

void ldb::BreakpointSite::Disable()
{
    if (!isEnable) return;

    errno = 0;
    std::uint64_t data = ptrace(PTRACE_PEEKDATA, process->Pid(), address, nullptr);
    if (errno != 0)
    {
        Error::SendErrno("Disabling breakpoint site failed");
    }

    auto restoredData = (data & ~0xff) | static_cast<std::uint8_t>(savedData);
    if (ptrace(PTRACE_POKEDATA, process->Pid(), address, restoredData) < 0)
    {
        Error::SendErrno("Disabling breakpoint site failed");
    }
    isEnable = false;
}