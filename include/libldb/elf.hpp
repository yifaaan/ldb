#pragma once


#include <filesystem>
#include <elf.h>
#include <vector>


#include "libldb/types.hpp"

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
    private:
        void BuildSectionMap();

        int fd;
        std::filesystem::path path;
        std::size_t fileSize;
        std::byte* data;
        Elf64_Ehdr header;
        std::vector<Elf64_Shdr> sectionHeaders;
        std::unordered_map<std::string_view, Elf64_Shdr*> sectionMap;
    };
}