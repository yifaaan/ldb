#pragma once

#include <array>
#include <vector>
#include <cstdint>
#include <cassert>

namespace ldb
{
    using Byte64 = std::array<std::byte, 8>;
    using Byte128 = std::array<std::byte, 16>;


    class FileAddr;
    class Elf;

    // the actual virtual addresses in the executing program after loading the ELF file into memory
    class VirtAddr
    {
    public:
		VirtAddr() = default;
		explicit VirtAddr(std::uint64_t _addr)
                : addr(_addr) 
        {
        }

        std::uint64_t Addr() const { return addr; }

        // symbol table gives us a function’s address information in the form of file addresses.
        FileAddr ToFileAddr(const Elf& elf) const;

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

    template <typename T>
    class Span
    {
    public:
        Span() = default;
        Span(T* _data, std::size_t _size) : data(_data), size(_size) { }
        Span(T* _data, T* _end) : data(_data), size(_end - _data) { }
        template <typename U>
        Span(const std::vector<U>& vec) : data(vec.data()), size(vec.size()) { }

        T* Begin() const { return data; }
        T* End() const { return data + size; }
        std::size_t Size() const { return size; }
        T& operator[](std::size_t n) const { return *(data + n); }

    private:
        T* data = nullptr;
        std::size_t size = 0;
    };

    enum class StoppointMode
    {
        write,
        readWrite,
        execute,
    };

    // virtual addresses specified in the ELF file
    class FileAddr
    {
    public:
        FileAddr() = default;
        FileAddr(const Elf& elf, std::uint64_t _addr)
            : elf(&elf)
            , addr(_addr)
        { }

        std::uint64_t Addr() const { return addr; }

        VirtAddr ToVirtAddr() const;

        const Elf* ElfFile() const { return elf; }



        FileAddr operator+(std::int64_t offset) const { return FileAddr{ *elf, addr + offset }; }
		FileAddr operator-(std::int64_t offset) const { return FileAddr{ *elf, addr - offset }; }
        FileAddr& operator+=(std::uint64_t offset)
        {
			addr += offset;
			return *this;
        }
        FileAddr& operator-=(std::uint64_t offset)
        {
			addr -= offset;
			return *this;
        }
		bool operator==(const FileAddr& other) const 
        { 
            return elf == other.elf && addr == other.addr; 
        }
		bool operator!=(const FileAddr& other) const 
        { 
            return elf != other.elf || addr != other.addr; 
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

    // absolute offsets from the start of the object file
    class FileOffset
    {
    public:
        FileOffset() = default;
        FileOffset(const Elf& elf, std::uint64_t _offset)
            : elf(&elf)
            , offset(_offset)
        { }

        std::uint64_t Offset() const { return offset; }

        const Elf* ElfFile() const { return elf; }
        
    private:
        const Elf* elf = nullptr;
        std::uint64_t offset = 0;
    };
}