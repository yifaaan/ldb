#ifndef LDB_TYPES_HPP
#define LDB_TYPES_HPP

#include <array>
#include <cstddef>
#include <cstdint>


namespace ldb
{
    using byte64 = std::array<std::byte, 8>;
    using byte128 = std::array<std::byte, 16>;

    class VirtAddr
    {
    public:
        VirtAddr() = default;

        explicit VirtAddr(std::uint64_t _addr) : addr(_addr) {}

        std::uint64_t Addr() const { return addr; }

        VirtAddr operator+(std::int64_t offset) const
        {
            return VirtAddr(addr + offset);
        }

        VirtAddr operator-(std::int64_t offset) const
        {
            return VirtAddr(addr - offset);
        }

        VirtAddr& operator+=(std::int64_t offset)
        {
            addr += offset;
            return *this;
        }

        VirtAddr& operator-=(std::int64_t offset)
        {
            addr -= offset;
            return *this;
        }

        auto operator<=>(const VirtAddr& other) const
        {
            return addr <=> other.addr;
        }

    private:
        std::uint64_t addr = 0;
    };
}

#endif