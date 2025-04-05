#pragma once

#include <array>
#include <cstdint>

namespace ldb
{
    using Byte64 = std::array<std::byte, 8>;
    using Byte128 = std::array<std::byte, 16>;

    class VirtAddr
    {
    public:
		VirtAddr() = default;
		explicit VirtAddr(std::uint64_t _addr)
                : addr(_addr) 
        {
        }

        std::uint64_t Addr() const { return addr; }

        VirtAddr operator+(std::int64_t offset) const { return VirtAddr{ addr + offset }; }
		VirtAddr operator-(std::int64_t offset) const { return VirtAddr{ addr - offset }; }
        VirtAddr& operator+=(std::uint64_t offset)
        {
			addr += offset;
			return *this;
        }
        VirtAddr& operator-=(std::uint64_t offset)
        {
			addr -= offset;
			return *this;
        }
		bool operator==(const VirtAddr& other) const { return addr == other.addr; }
		bool operator!=(const VirtAddr& other) const { return addr != other.addr; }
		bool operator<(const VirtAddr& other) const { return addr < other.addr; }
		bool operator<=(const VirtAddr& other) const { return addr <= other.addr; }
		bool operator>(const VirtAddr& other) const { return addr > other.addr; }
		bool operator>=(const VirtAddr& other) const { return addr >= other.addr; }

    private:
        std::uint64_t addr = 0;
    };
}