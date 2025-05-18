#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

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

    template <typename T>
    class span
    {
    public:
        span() = default;
        span(T* data, std::size_t size)
            : data_{data}
            , size_{size}
        {
        }

        span(T* begin, T* end)
            : data_{begin}
            , size_{end - begin}
        {
        }

        template <typename U>
            requires std::is_convertible_v<U, T>
        span(const std::vector<U>& vec)
            : data_{vec.data()}
            , size_{vec.size()}
        {
        }

        T* begin() const
        {
            return data_;
        }

        T* end() const
        {
            return data_ + size_;
        }

        std::size_t size() const
        {
            return size_;
        }

        T& operator[](std::size_t index) const
        {
            return data_[index];
        }

    private:
        T* data_;
        std::size_t size_;
    };

    /// @brief 硬件断点模式，用于设置硬件断点时选择触发条件
    enum class stoppoint_mode
    {
        /// 写入某个地址触发
        write,
        /// 读写某个地址触发
        read_write,
        /// 执行某个地址触发
        execute,
    };
} // namespace ldb
