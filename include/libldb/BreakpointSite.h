#pragma once

#include <cstdint>

#include <libldb/Types.h>

namespace ldb
{
    class Process;

    class BreakpointSite
    {
    public:
        BreakpointSite() = delete;
        BreakpointSite(const BreakpointSite&) = delete;
        BreakpointSite& operator=(const BreakpointSite&) = delete;

        using IdType = int32_t;

        IdType Id() const { return id; }

        void Enable();
        void Disable();

        bool IsEnabled() const { return isEnabled; }
        VirtAddr Address() const { return address; }
        bool AtAddress(VirtAddr addr) const { return address == addr; }

        bool Inrange(VirtAddr low, VirtAddr high) const { return address >= low && address < high; }

    
    private:
        friend class Process;
        BreakpointSite(Process& proc, VirtAddr addr);

        IdType id;
        Process* process;
        VirtAddr address;
        bool isEnabled;
        std::byte savedData;
    };
}