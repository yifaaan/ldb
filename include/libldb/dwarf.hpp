#pragma once

#include <unordered_map>
#include <vector>

#include <libldb/detail/dwarf.h>
#include <libldb/types.hpp>

namespace ldb 
{
    struct AttrSpec
    {
        /// 属性
        std::uint64_t attr;
        /// 形式
        std::uint64_t form;
    };

    struct Abbrev
    {
        /// 缩写码
        std::uint64_t code;
        /// 标签
        std::uint64_t tag;
        /// 是否有子节点
        bool hasChildren;
        /// 属性规格列表
        std::vector<AttrSpec> attrSpecs;
    };

    class Die;
    class Dwarf;
    class CompileUnit
    {
    public:
        CompileUnit(Dwarf& _parent, Span<const std::byte> _data, std::size_t _abbrevOffset)
            :parent{ &_parent }
            ,data{ _data }  
            ,abbrevOffset{ _abbrevOffset }
        {}

        const Dwarf* DwarfInfo() const { return parent; }

        Span<const std::byte> Data() const { return data; }

        const std::unordered_map<std::uint64_t, Abbrev>& AbbrevTable() const;

        ldb::Die Root() const;

    private:
        // struct CompileUnitHeader 
        // {
        //    uint32_t unit_length;      // 编译单元的总长度
        //    uint16_t version;          // DWARF格式版本号
        //    uint32_t debug_abbrev_offset;  // 缩写表的偏移量
        //    uint8_t  address_size;     // 目标机器的地址大小
        // };

        /// 所属的Dwarf
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

        const std::unordered_map<std::uint64_t, Abbrev>& GetAbbrevTable(std::size_t offset);

        const std::vector<std::unique_ptr<CompileUnit>>& CompileUnits() const { return compileUnits; }

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

    class Die
    {
    public:
        explicit Die(const std::byte* _next)
            :next{ _next }
        {}

        Die(const std::byte* _pos,
            const CompileUnit* _compileUnit,
            const Abbrev* _abbrev,
            std::vector<const std::byte*> _attrLocs,
            const std::byte* _next)
            :pos { _pos }
            ,next{ _next }
            ,compileUnit{ _compileUnit }
            ,abbrev{ _abbrev }
            ,attrLocs{ std::move(_attrLocs) }
        {}

        const CompileUnit* Cu() const { return compileUnit; }

        const Abbrev* AbbrevEntry() const { return abbrev; }

        const std::byte* Position() const { return pos; }

        const std::byte* Next() const { return next; }

        
    private:
        /// 当前DIE的开始位置
        const std::byte* pos = nullptr;
        /// 当前DIE的所属的编译单元
        const CompileUnit* compileUnit = nullptr;
        /// 当前DIE的所属的缩写表
        const Abbrev* abbrev = nullptr;
        /// 当前DIE的属性位置列表
        std::vector<const std::byte*> attrLocs;
        /// 下一个DIE的开始位置
        const std::byte* next = nullptr;
    };
}
