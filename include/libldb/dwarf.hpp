#pragma once


#include <cstddef>
#include <iterator>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <memory>
#include <optional>

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

    class CompileUnit;
    class Die;
    class Attr
    {
    public:
        Attr(const CompileUnit* _compileUnit, std::uint64_t _type, std::uint64_t _form, const std::byte* _location)
            : compileUnit(_compileUnit)
            , type(_type)
            , form(_form)
            , location(_location)
        {}

        std::uint64_t Name() const { return type; }
        std::uint64_t Form() const { return form; }

        FileAddr AsAddress() const;

        std::uint32_t AsSectionOffset() const;

        Span<const std::byte> AsBlock() const;

        std::uint64_t AsInt() const;

        std::string_view AsString() const;

        Die AsReference() const;

    private:
        const CompileUnit* compileUnit;
        std::uint64_t type;
        std::uint64_t form;
        const std::byte* location;
    };

    class Die;
    class Dwarf;
    class CompileUnit
    {
    public:
        CompileUnit(Dwarf& _parent, Span<const std::byte> _data, std::size_t _abbrevOffset)
            : parent(&_parent)
            , data(_data)
            , abbrevOffset(_abbrevOffset)
        {}

        const Dwarf* DwarfInfo() const { return parent; }

        Span<const std::byte> Data() const { return data; }

        const std::unordered_map<std::uint64_t, Abbrev>& AbbrevTable() const;

        Die Root() const;
    
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

        const Elf* ElfFile() const { return elf;}

        const std::unordered_map<std::uint64_t, Abbrev>& GetAbbrevTable(std::size_t offset);
    
        const std::vector<std::unique_ptr<CompileUnit>>& CompileUnits() const
        {
            return compileUnits;
        }

    private:
        const Elf* elf;

        std::unordered_map<std::size_t, std::unordered_map<std::uint64_t, Abbrev>> abbrevTables;
    
        std::vector<std::unique_ptr<CompileUnit>> compileUnits;
    };

    class Die
    {
    public:
        explicit Die(const std::byte* _next)
            : next(_next)
        {}

        Die(const std::byte* _position,
            const CompileUnit* _compileUnit,
            const Abbrev* _abbrev,
            std::vector<const std::byte*> _attrLocations,
            const std::byte* _next)
            : position(_position)
            , compileUnit(_compileUnit)
            , abbrev(_abbrev)
            , attrLocations(std::move(_attrLocations))
            , next(_next)
        {}

        const CompileUnit* Cu() const { return compileUnit; }

        const Abbrev* AbbrevEntry() const { return abbrev; }

        const std::byte* Position() const { return position; }

        const std::byte* Next() const { return next; }

        // the attribute must different in a same DIE
        bool Contains(std::uint64_t attribute) const;

        // get the value of the attribute
        Attr operator[](std::uint64_t attribute) const;

        FileAddr LowPc() const;
        FileAddr HighPc() const;


        class ChildrenRange;
        ChildrenRange Children() const;

    private:
        const std::byte* position = nullptr;
        const CompileUnit* compileUnit = nullptr;
        const Abbrev* abbrev = nullptr;
        const std::byte* next = nullptr;
        std::vector<const std::byte*> attrLocations;
    };

    class Die::ChildrenRange
    {
    public:
        ChildrenRange(Die _die)
            : die(std::move(_die))
        {}

        class iterator
        {
        public:
            using value_type = Die;
            using reference = const Die&;
            using pointer = const Die*;
            using difference_type = std::ptrdiff_t;
            using iterator_category = std::forward_iterator_tag;

            iterator() = default;
            iterator(const iterator&) = default;
            iterator& operator=(const iterator&) = default;

            explicit iterator(const Die& _die);

            const Die& operator*() const { return *die; }
            const Die* operator->() const { return &die.value(); }

            iterator& operator++();
            iterator operator++(int);

            bool operator==(const iterator& rhs) const;
            bool operator!=(const iterator& rhs) const
            {
                return !(*this == rhs);
            }
        private:
            std::optional<Die> die;
        };

        iterator begin() const
        {
            if (die.abbrev->hasChildren)
            {
                return iterator{die};
            }
            return end();
        }

        iterator end() const { return {}; }

    private:
        Die die;
    };
}