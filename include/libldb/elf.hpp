#pragma once



#include <elf.h>

#include <map>
#include <optional>
#include <filesystem>
#include <vector>
#include <unordered_map>



#include <libldb/types.hpp>

namespace ldb
{
    class Elf
    {
    public:
        explicit Elf(const std::filesystem::path& path);
        ~Elf();

        Elf(const Elf&) = delete;
        Elf& operator=(const Elf&) = delete;

        std::filesystem::path Path() const { return path; }

        const Elf64_Ehdr& GetHeader() const { return header; }
        
        void ParseSectionHeaders();

        std::string_view GetSectionName(std::size_t index) const;

        std::optional<const Elf64_Shdr*> GetSection(std::string_view name) const;

        Span<const std::byte> GetSectionContents(std::string_view name) const;

        /// general string table: .strtab
        std::string_view GetString(std::size_t index) const;

        /// get load bias
        VirtAddr LoadBias() const { return loadBias; }

        void NotifyLoaded(VirtAddr address) { loadBias = address; }

        /// retrieve the section to which a file address belongs
        const Elf64_Shdr* GetSectionContainingAddress(FileAddr addr) const;

        /// retrieve the section to which a virtual address belongs
        const Elf64_Shdr* GetSectionContainingAddress(VirtAddr addr) const;

        std::optional<FileAddr> GetSectionStartAddress(std::string_view name) const
        {
            if (auto sectionHeader = GetSection(name); sectionHeader)
            {
                return FileAddr{ *this, sectionHeader.value()->sh_addr };
            }
            return std::nullopt;
        }

        /// parse symbol table
        void ParseSymbolTable();

        std::vector<const Elf64_Sym*> GetSymbolsByName(std::string_view name) const;

        std::optional<const Elf64_Sym*> GetSymbolAtAddress(FileAddr addr) const;

        std::optional<const Elf64_Sym*> GetSymbolAtAddress(VirtAddr Addr) const;

        std::optional<const Elf64_Sym*> GetSymbolContainingAddress(FileAddr addr) const;

        std::optional<const Elf64_Sym*> GetSymbolContainingAddress(VirtAddr addr) const;
        
    private:
        void BuildSectionMap();

        void BuildSymbolMaps();

        int fd;
        std::filesystem::path path;
        std::size_t fileSize;
        std::byte* data;
        Elf64_Ehdr header;
        std::vector<Elf64_Shdr> sectionHeaders;
        std::unordered_map<std::string_view, Elf64_Shdr*> sectionMap;
        VirtAddr loadBias;
        std::vector<Elf64_Sym> symbolTable;
        std::unordered_multimap<std::string_view, Elf64_Sym*> symbolNameMap;

        static constexpr auto RangeComparator = [](const auto& a, const auto& b)
        {
            return a.first < b.first;
        };
        std::map<std::pair<FileAddr, FileAddr>, Elf64_Sym*, decltype(RangeComparator)> symbolAddrMap;
    };
}