#include <libldb/BreakpointSite.h>
#include <libldb/Process.h>

#include <cerrno>
#include <format>
#include <sys/ptrace.h>

namespace
{
    auto GetNextId()
    {
        static ldb::BreakpointSite::IdType id = 0;
        return ++id;
    }
} // namespace

namespace ldb
{
    BreakpointSite::BreakpointSite(Process& proc, VirtAddr addr)
        : process(&proc), address(addr), isEnabled(false), savedData{}
    {
        id = GetNextId();
    }

    void BreakpointSite::Enable()
    {
        if (isEnabled)
        {
            return;
        }

        errno = 0;
        uint64_t data = ptrace(PTRACE_PEEKDATA, process->Pid(), address.Address(), nullptr);
        if (errno != 0)
        {
            Error::SendErrno(std::format("Failed to enable breakpoint at {}", address.Address()));
        }

        savedData = static_cast<std::byte>(data & 0xFF);
        uint64_t dataWithInt3 = (data & ~0xFF) | 0xcc;
        if (ptrace(PTRACE_POKEDATA, process->Pid(), address.Address(), dataWithInt3) < 0)
        {
            Error::SendErrno(std::format("Failed to enable breakpoint at {}", address.Address()));
        }
        isEnabled = true;
    }

    void BreakpointSite::Disable()
    {
        if (!isEnabled)
        {
            return;
        }

        errno = 0;
        uint64_t data = ptrace(PTRACE_PEEKDATA, process->Pid(), address.Address(), nullptr);
        if (errno != 0)
        {
            Error::SendErrno(std::format("Failed to disable breakpoint at {}", address.Address()));
        }
        auto restoredData = (data & ~0xFF) | static_cast<uint8_t>(savedData);
        if (ptrace(PTRACE_POKEDATA, process->Pid(), address.Address(), restoredData) < 0)
        {
            Error::SendErrno(std::format("Failed to disable breakpoint at {}", address.Address()));
        }
        isEnabled = false;
    }
} // namespace ldb