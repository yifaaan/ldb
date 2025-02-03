#pragma once

#include <unordered_map>
#include <vector>

#include <libldb/detail/dwarf.h>
#include <libldb/types.hpp>

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

    class Dwarf;
    class CompileUnit
    {
    public:
        CompileUnit(Dwarf& _parent, Span<const std::byte> _data, std::size_t _abbrevOffset)
            :parent{ &_parent }
            ,data{ _data }  
            ,abbrevOffset{ _abbrevOffset }
        {}

        auto DwarfInfo() -> const Dwarf* const { return parent; }

        auto Data() const { return data; }

        const auto& AbbrevTable() const;

    private:
        Dwarf* parent;
        Span<const std::byte> data;
        std::size_t abbrevOffset;
    };

    class Elf;
    class Dwarf 
    {
    public:
        Dwarf(const Elf& parent);
        const Elf* ElfFile() const { return elf; }

        const auto& GetAbbrevTable(std::size_t offset);

        const auto& CompileUnits() const { return compileUnits; }
        
    private:
        const Elf* elf;

        /// `.debug_abbrev`段中，以`offset`为key，以`abbrev_table`为value的map
        ///
        /// Each compile unit in the `.debug_info` section uses exactly one abbreviation table, but
        /// different compile units may share the same table.
        ///
        /// `abbrev_table`的每个表项有一个缩写码(abbreviation code):
        ///
        /// `从1开始递增的整数`
        ///
        /// `表示该表项在缩写表中的顺序`
        ///
        /// 每个表项的格式为（encoded as an ULEB128 integer）:
        ///
        /// [缩写码] [标签-对应dwarf.h中的DW_TAG_常量] [子DIE标志位] [属性规格列表-类型，形式] [终止符]
        std::unordered_map<std::size_t, std::unordered_map<std::uint64_t, Abbrev>> abbrevTables;
        std::vector<std::unique_ptr<CompileUnit>> compileUnits;
    };

}

