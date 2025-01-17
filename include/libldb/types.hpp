#ifndef LDB_TYPES_HPP
#define LDB_TYPES_HPP

#include <array>
#include <vector>
#include <cstddef>
#include <cstdint>


namespace ldb
{
    using byte64 = std::array<std::byte, 8>;
    using byte128 = std::array<std::byte, 16>;

    enum class StoppointMode
    {
        Write,
        ReadWrite,
        Execute,
    };

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

        auto operator<=>(const VirtAddr& other) const = default;

    private:
        std::uint64_t addr = 0;
    };

    template<typename T>
    class Span
    {
    public:
        Span() = default;
        Span(T* _data, std::size_t _size) : data(_data), size(_size) {}
        Span(T* _data, T* _end) : data(_data), size(_end - _data) {}
        template<typename U>
        Span(const std::vector<U>& vec) : data(vec.data()), size(vec.size()) {}

        T* Begin() const { return data; }
        T* End() const { return data + size; }
        std::size_t Size() const { return size; }
        T& operator[](std::size_t n) { return *(data + n); }
    private:
        T* data = nullptr;
        std::size_t size = 0;
    };
}

#endif