#include <libldb/watchpoint.hpp>
#include <libldb/process.hpp>

namespace
{
    auto GetNextId() -> ldb::Watchpoint::IdType
    {
        static ldb::Watchpoint::IdType id = 0;
        return ++id;
    }
}

ldb::Watchpoint::Watchpoint(Process& proc, VirtAddr _address, StoppointMode _mode, std::size_t _size)
    :process(&proc)
    ,address(_address)
    ,mode(_mode)
    ,size(_size)
    ,isEnabled(false)
{
    if ((address.Addr() & (size - 1)) != 0)
    {
        Error::Send("Watchpoint must be aligned to size");
    }
    id = GetNextId();
}

void ldb::Watchpoint::Enable()
{
    if (isEnabled) return;

    hardwareRegisterIndex = process->SetWatchpoint(id, address, mode, size);
    isEnabled = true;
}

void ldb::Watchpoint::Disable()
{
    if (!isEnabled) return;

    process->ClearHardwareStoppoint(hardwareRegisterIndex);
    hardwareRegisterIndex = -1;
    isEnabled = false;
}