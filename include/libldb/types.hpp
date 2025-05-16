#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace ldb
{
    /// @brief 64 位字节数组
    using byte64 = std::array<std::byte, 8>;
    /// @brief 128 位字节数组
    using byte128 = std::array<std::byte, 16>;

    class virt_addr
    {
    public:
        virt_addr() = default;
        explicit virt_addr(std::uint64_t addr)
            : addr_{addr}
        {
        }

        std::uint64_t addr() const
        {
            return addr_;
        }

        virt_addr operator+(std::int64_t offset) const
        {
            return virt_addr{addr_ + offset};
        }

        virt_addr operator-(std::int64_t offset) const
        {
            return virt_addr{addr_ - offset};
        }

        virt_addr& operator+=(std::int64_t offset)
        {
            addr_ += offset;
            return *this;
        }

        virt_addr& operator-=(std::int64_t offset)
        {
            addr_ -= offset;
            return *this;
        }

        bool operator==(const virt_addr& other) const
        {
            return addr_ == other.addr_;
        }

        bool operator!=(const virt_addr& other) const
        {
            return !(*this == other);
        }

        bool operator<(const virt_addr& other) const
        {
            return addr_ < other.addr_;
        }

        bool operator>(const virt_addr& other) const
        {
            return addr_ > other.addr_;
        }

        bool operator<=(const virt_addr& other) const
        {
            return !(*this > other);
        }

        bool operator>=(const virt_addr& other) const
        {
            return !(*this < other);
        }

    private:
        std::uint64_t addr_;
    };
} // namespace ldb
