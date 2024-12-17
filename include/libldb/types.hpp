#ifndef LDB_TYPES_HPP
#define LDB_TYPES_HPP

#include <array>
#include <cstddef>
#include <cstdint>

namespace ldb
{
    using byte64 = std::array<std::byte, 8>;
    using byte128 = std::array<std::byte, 16>;

    /// Representing virtual addresses
    class virt_addr
    {
    public:
        virt_addr() = default;
        explicit virt_addr(std::uint64_t addr) : addr_(addr)
        {
        }

        std::uint64_t addr() const
        {
            return addr_;
        }

        virt_addr operator+(std::uint64_t offset) const
        {
            return virt_addr(addr_ + offset);
        }
        virt_addr operator-(std::uint64_t offset) const
        {
            return virt_addr(addr_ - offset);
        }
        virt_addr& operator+=(std::uint64_t offset)
        {
            addr_ += offset;
            return *this;
        }
        virt_addr& operator-=(std::uint64_t offset)
        {
            addr_ -= offset;
            return *this;
        }
        auto operator<=>(const virt_addr& other) const = default;

    private:
        std::uint64_t addr_ = 0;
    };
} // namespace ldb

#endif
