#pragma once

#include <cstdint>
#include <cstddef>
#include <libldb/types.hpp>

namespace ldb
{
    class Process;

    class Watchpoint
    {
    public:
        using IdType = std::int32_t;

        Watchpoint() = delete;
        Watchpoint(const Watchpoint&) = delete;
        Watchpoint& operator=(const Watchpoint&) = delete;

        auto Enable() -> void;

        auto Disable() -> void;

        auto IsEnabled() const -> bool { return isEnabled; }

        auto Address() const -> VirtAddr { return address; }

        auto AtAddress(VirtAddr addr) const -> bool { return address == addr; }

        auto InRange(VirtAddr low, VirtAddr high) const -> bool
        {
            return low <= address and address < high;
        }

        auto Id() const -> IdType { return id; }

        auto Mode() const -> StoppointMode { return mode; }
        auto Size() const -> std::size_t { return size; }

        auto Data() const -> std::uint64_t { return data; }
        auto PreviousData() const -> std::uint64_t { return previousData; }
        auto UpdateData() -> void;

    private:
        friend Process;
        Watchpoint(Process& proc, VirtAddr _address, StoppointMode _mode, std::size_t _size);


        Process*        process;
        VirtAddr        address;
        StoppointMode   mode;
        std::size_t     size;
        bool            isEnabled;
        /// debugger register index
        int             hardwareRegisterIndex = -1;
        IdType          id;
        std::uint64_t   data = 0;
        std::uint64_t   previousData = 0;
    };
}