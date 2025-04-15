#pragma once

#include <unordered_map>
#include <vector>
#include <cstdint>

#include <libldb/detail/dwarf.h>

namespace ldb
{
    
    struct AttrSpec
    {
        std::uint64_t attr;
        std::uint64_t form;
    };

    struct Abbrev
    {
        std::uint64_t code;
        std::uint64_t tag;
        bool hasChildren;
        std::vector<AttrSpec> attrSpecs;
    };

    class Elf;
    class Dwarf
    {
    public:
        Dwarf(const Elf& parent);

        const Elf* ElfFile() const { return elf;}

        const std::unordered_map<std::uint64_t, Abbrev>& GetAbbrevTable(std::size_t offset);
    private:
        const Elf* elf;

        std::unordered_map<std::size_t, std::unordered_map<std::uint64_t, Abbrev>> abbrevTables;
    };
}