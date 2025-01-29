#ifndef LDB_TYPES_HPP
#define LDB_TYPES_HPP

#include <array>
#include <vector>
#include <cstddef>
#include <cstdint>
#include <cassert>

// 不能加上这行，Elf用了VirtAddr,而VritAddr用了Elf,不能两个都相互包含
// #include <libldb/elf.hpp>


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

    // 解决方法如下，声明Elf就行,在elf.hpp中include当前文件
    
    class FileAddr;
    class Elf;
    class VirtAddr
    {
    public:
        VirtAddr() = default;

        explicit VirtAddr(std::uint64_t _addr) : addr(_addr) {}

        std::uint64_t Addr() const { return addr; }

        FileAddr ToFileAddr(const Elf& elf) const;

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

    /// virtual address specified in the ELF file
    class FileAddr
    {
    public:
        FileAddr() = default;
        FileAddr(const Elf& _elf, std::uint64_t _addr)
            :elf{ &_elf }
            ,addr{ _addr }
        {}

        std::uint64_t Addr() const { return addr; }

        const Elf* ElfFile() const { return elf; }

        VirtAddr ToVirtAddr() const;

        FileAddr operator+(std::int64_t offset) const
        {
            return FileAddr{ *elf, addr + offset };
        }

        FileAddr operator-(std::int64_t offset) const
        {
            return FileAddr{ *elf, addr - offset };
        }

        FileAddr& operator+=(std::int64_t offset)
        {
            addr += offset;
            return *this;
        }

        FileAddr& operator-=(std::int64_t offset)
        {
            addr -= offset;
            return *this;
        }
        
        // TODO: replace with this
        // auto operator<=>(const FileAddr& other) const  = default;

        bool operator==(const FileAddr& other) const
        {
            return addr == other.addr and elf == other.elf;
        }

        bool operator!= (const FileAddr& other) const
        {
            return !(*this == other);
        }

        bool operator<(const FileAddr& other) const
        {
            assert(elf == other.elf);
            return addr < other.addr;
        }

        bool operator<=(const FileAddr& other) const
        {
            assert(elf == other.elf);
            return addr <= other.addr;
        }

        bool operator>(const FileAddr& other) const
        {
            assert(elf == other.elf);
            return addr > other.addr;
        }

        bool operator>=(const FileAddr& other) const
        {
            assert(elf == other.elf);
            return addr >= other.addr;
        }


    private:
        const Elf* elf = nullptr;
        std::uint64_t addr = 0;
    };

    /// absolute offset from the start of the obj file
    class FileOffset
    {
    public:
        FileOffset() = default;
        FileOffset(const Elf& _elf, std::uint64_t _offset)
            :elf{ &_elf }
            ,offset{ _offset }
        {}

        std::uint64_t Offset() const { return offset; }

        const Elf* ElfFile() const { return elf; }

    private:
        const Elf* elf = nullptr;
        std::uint64_t offset = 0;
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