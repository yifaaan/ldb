#pragma once

#include <cstddef>

#include <libldb/types.hpp>

namespace ldb
{
    class Process;
    class BreakpointSite
    {
    public:
        using IdType = std::int32_t;

        BreakpointSite() = delete;
        BreakpointSite(const BreakpointSite&) = delete;
        BreakpointSite& operator=(const BreakpointSite&) = delete;

        void Enable();

        void Disable();

        bool isEnabled() const { return isEnable; }

        VirtAddr Address() const { return address; }

        bool AtAddress(VirtAddr addr) const { return address == addr; }

        bool InRange(VirtAddr low, VirtAddr high) const
        {
            return low <= address and address < high;
        }

        IdType Id() const { return id; }

    private:
        BreakpointSite(Process& proc, VirtAddr _address);
        friend Process;

    private:
        IdType      id;
        Process*    process;
        VirtAddr    address;
        bool        isEnable;
        /// the data replace with the int3
        std::byte   savedData;
    };
}