#include <algorithm>

#include <libldb/dwarf.hpp>
#include <libldb/types.hpp>
#include <libldb/bit.hpp>
#include <libldb/elf.hpp>
#include <libldb/error.hpp>

namespace
{
    class Cursor
    {
    public:
        explicit Cursor(ldb::Span<const std::byte> _data)
            : data(_data)
            , pos(_data.Begin())
        {}

        Cursor& operator++()
        {
            ++pos;
            return *this;
        }

        Cursor& operator+=(std::size_t n)
        {
            pos += n;
            return *this;
        }

        const std::byte* Position()
        {
            return pos;
        }

        bool Finished() const
        {
            return pos >= data.End();
        }

        template <typename T>
        T FixInt()
        {
            auto t = ldb::FromBytes<T>(pos);
            pos += sizeof(T);
            return t;
        }

        std::uint8_t U8()
        {
            return FixInt<std::uint8_t>();
        }

        std::uint16_t U16()
        {
            return FixInt<std::uint16_t>();
        }

        std::uint32_t U32()
        {
            return FixInt<std::uint32_t>();
        }

        std::uint64_t U64()
        {
            return FixInt<std::uint64_t>();
        }

        std::int8_t S8()
        {
            return FixInt<std::int8_t>();
        }

        std::int16_t S16()
        {
            return FixInt<std::int16_t>();
        }

        std::int32_t S32()
        {
            return FixInt<std::int32_t>();
        }

        std::int64_t S64()
        {
            return FixInt<std::int64_t>();
        }
        

        std::string_view String()
        {
            auto null = std::find(pos, data.End(), std::byte{0});
            std::string_view ret{reinterpret_cast<const char*>(pos), static_cast<std::size_t>(null - pos)};
            pos = null + 1;
            return ret;
        }

        // 小端存储
        // 编码整数624,485

        // 二进制表示：100110 0001110 1100101

        // 补齐到7的倍数位数：010011000011101100101

        // 拆分为7位组：0100110 0001110 1100101

        // 加前缀位：00100110 10001110 11100101

        // 有符号整数编码（SLEB128）：

        // 先用二补码表示整数，位宽为7的倍数。

        // 然后按ULEB128相同方式拆分和加前缀。
        std::uint64_t Uleb128()
        {
            std::uint64_t ret = 0;
            int shift = 0;
            std::uint8_t byte = 0;
            do
            {
                byte = U8();
                auto mask = static_cast<std::uint64_t>(byte & 0x7f);
                ret |= mask << shift;
                shift += 7;
            } while ((byte & 0x80) != 0);
            return ret;
        }
        
        // 举例：编码-123,456

        // 123,456二进制：11110001001000000

        // 补齐到7的倍数：000011110001001000000

        // 取反：111100001110110111111

        // 加1：111100001110111000000

        // 拆分7位组：1111000 0111011 1000000

        // 加前缀位：01111000 10111011 11000000
        std::int64_t Sleb128()
        {
            std::uint64_t ret = 0;
            int shift = 0;
            std::uint8_t byte = 0;
            do
            {
                byte = U8();
                auto mask = static_cast<std::uint64_t>(byte & 0x7f);
                ret |= mask << shift;
                shift += 7;
            } while ((byte & 0x80) != 0);

            if (shift < 64 && (byte & 0x40) != 0)
            {
                ret |= (~static_cast<std::uint64_t>(0) << shift);
            }
            return static_cast<std::int64_t>(ret);
        }

        void SkipForm(std::uint64_t form)
        {
            switch (form)
            {
            case DW_FORM_flag_present:
                break;
            
            case DW_FORM_data1:
            case DW_FORM_ref1:
            case DW_FORM_flag:
                pos += 1;
                break;
            
            case DW_FORM_data2:
            case DW_FORM_ref2:
                pos += 2;
                break;
            
            case DW_FORM_data4:
            case DW_FORM_ref4:
            case DW_FORM_ref_addr:
            case DW_FORM_sec_offset:
            case DW_FORM_strp:
                pos += 4;
                break;
            
            case DW_FORM_data8:
            case DW_FORM_addr:
                pos += 8;
                break;

            case DW_FORM_sdata:
                Sleb128();
            
            case DW_FORM_udata:
            case DW_FORM_ref_udata:
                Uleb128();
                break;
            
            case DW_FORM_block1:
                pos += U8();
                break;
            case DW_FORM_block2:
                pos += U16();
                break;
            case DW_FORM_block4:
                pos += U32();
                break;
            case DW_FORM_block:
            case DW_FORM_exprloc:
                pos += Uleb128();
                break;
            case DW_FORM_string:
                while (!Finished() && *pos != std::byte{0}) ++pos;
                ++pos;
                break;
            case DW_FORM_indirect:
                SkipForm(Uleb128());
                break;

            default: ldb::Error::Send("Unrecognized DWARF form");
            }
        }


    private:
        ldb::Span<const std::byte> data;
        const std::byte* pos;
    };

    std::unordered_map<std::uint64_t, ldb::Abbrev> ParseAbbrevTable(const ldb::Elf& obj, std::size_t offset)
    {
        Cursor cursor{obj.GetSectionContents(".debug_abbrev")};
        cursor += offset;

        std::unordered_map<std::uint64_t, ldb::Abbrev> table;
        std::uint64_t code = 0;
        do
        {
            code = cursor.Uleb128();
            auto tag = cursor.Uleb128();
            auto hasChildren = static_cast<bool>(cursor.U8());
            std::vector<ldb::AttrSpec> attrSpecs;
            std::uint64_t attr = 0;
            std::uint64_t form = 0;
            {
                attr = cursor.Uleb128();
                form = cursor.Uleb128();
                if (attr != 0 && form != 0)
                {
                    attrSpecs.emplace_back(attr, form);
                }
            } while (attr != 0 && form != 0);

            if (code != 0)
            {
                table.emplace(code, ldb::Abbrev{code, tag, hasChildren, std::move(attrSpecs)});
            }
        } while (code != 0);
        return table;
    }

    std::unique_ptr<ldb::CompileUnit> ParseCompileUnit(ldb::Dwarf& dwarf, const ldb::Elf& obj, Cursor cursor)
    {
        auto start = cursor.Position();
        auto size = cursor.U32();
        auto version = cursor.U16();
        auto abbrevOffset = cursor.U32();
        auto addrSize = cursor.U8();
        if (size == 0xffffffff)
        {
            ldb::Error::Send("Only DWARF32 is supported");
        }
        if (version != 4)
        {
            ldb::Error::Send("Only DWARF version 4 is supported");
        }
        if (addrSize != 8)
        {
            ldb::Error::Send("Invalid address size of DWARF");
        }

        size += sizeof(std::uint32_t);
        ldb::Span<const std::byte> data = {start, size};
        return std::make_unique<ldb::CompileUnit>(dwarf, data, abbrevOffset);
    }

    std::vector<std::unique_ptr<ldb::CompileUnit>> ParseCompileUnits(ldb::Dwarf& dwarf, const ldb::Elf& obj)
    {
        auto debugInfo = obj.GetSectionContents(".debug_info");
        Cursor cursor{debugInfo};

        std::vector<std::unique_ptr<ldb::CompileUnit>> units;
        while (!cursor.Finished())
        {
            auto unit = ParseCompileUnit(dwarf, obj, cursor);
            cursor += unit->Data().Size();
            units.push_back(std::move(unit));
        }
        return units;
    }

    ldb::Die ParseDie(const ldb::CompileUnit& compileUnit, Cursor cursor)
    {
        auto pos = cursor.Position();
        auto abbrevCode = cursor.Uleb128();
        if (abbrevCode == 0)
        {
            auto next = cursor.Position();
            return ldb::Die{next};
        }
        auto& abbrevTable = compileUnit.AbbrevTable();
        auto& abbrevEntry = abbrevTable.at(abbrevCode);

        std::vector<const std::byte*> attrLocs;
        attrLocs.reserve(abbrevEntry.attrSpecs.size());
        for (const auto& attr : abbrevEntry.attrSpecs)
        {
            attrLocs.push_back(cursor.Position());
            cursor.SkipForm(attr.form);
        }
        auto next = cursor.Position();
        return ldb::Die{pos, &compileUnit, &abbrevEntry,std::move(attrLocs), next};
    }
}

namespace ldb
{

    const std::unordered_map<std::uint64_t, Abbrev>& CompileUnit::AbbrevTable() const
    {
        return parent->GetAbbrevTable(abbrevOffset);
    }

    Die CompileUnit::Root() const
    {
        std::size_t headerSize = 11;
        Cursor cursor{{data.Begin() + headerSize, data.End()}};
        return ParseDie(*this, cursor);
    }

    Dwarf::Dwarf(const Elf& parent)
        : elf(&parent)
    {
        compileUnits = ParseCompileUnits(*this, parent);
    }

    const std::unordered_map<std::uint64_t, Abbrev>& Dwarf::GetAbbrevTable(std::size_t offset)
    {
        if (!abbrevTables.contains(offset))
        {
            abbrevTables.emplace(offset, ParseAbbrevTable(*elf, offset));
        }
        return abbrevTables.at(offset);
    }

    bool Die::Contains(std::uint64_t attribute) const
    {
        return std::ranges::find_if(abbrev->attrSpecs, [=](const auto spec)
        {
            return spec.attr == attribute;
        }) != std::end(abbrev->attrSpecs);
    }

    Attr Die::operator[](std::uint64_t attribute) const
    {
        auto& specs = abbrev->attrSpecs;
        for (int i = 0; i < specs.size(); i++)
        {
            if (specs[i].attr == attribute)
            {
                return {compileUnit, specs[i].attr, specs[i].form, attrLocations[i]};
            }
        }
        Error::Send("Attribute not found");
    }

    FileAddr Die::LowPc() const
    {
        return (*this)[DW_AT_low_pc].AsAddress();
    }

    FileAddr Die::HighPc() const
    {
        auto attr = (*this)[DW_AT_high_pc];
        FileAddr addr;
        if (attr.Form() == DW_FORM_addr)
        {
            addr = attr.AsAddress();
        }
        else
        {
            addr = LowPc() + attr.AsInt();
        }
        return addr;
    }

    Die::ChildrenRange Die::Children() const
    {
        return ChildrenRange{*this};
    }

    Die::ChildrenRange::iterator::iterator(const Die& _die)
    {
        Cursor nextCur{{_die.Next(), _die.compileUnit->Data().End()}};
        die = ParseDie(*_die.compileUnit, nextCur);
    }

    bool Die::ChildrenRange::iterator::operator==(const iterator& rhs) const
    {
        auto lhsNull = !die.has_value() || !die->AbbrevEntry();
        auto rhsNull = !rhs.die.has_value() || !rhs.die->AbbrevEntry();
        if (lhsNull && rhsNull) return true;
        if (lhsNull || rhsNull) return false;

        return die->abbrev == rhs->abbrev && die->Next() == rhs->Next();
    }

    Die::ChildrenRange::iterator& Die::ChildrenRange::iterator::operator++()
    {
        if (!die.has_value() || !die->abbrev) return *this;
        if (!die->abbrev->hasChildren)
        {
            Cursor nextCur{{die->next, die->compileUnit->Data().End()}};
            die = ParseDie(*die->compileUnit, nextCur);
        }
        else if (die->Contains(DW_AT_sibling))
        {
            die = die.value()[DW_AT_sibling].AsReference();
        }
        else
        {
            iterator subChild{*die};
            while (subChild->abbrev) ++subChild;
            Cursor nextCur{{subChild->next, die->compileUnit->Data().End()}};
            die = ParseDie(*die->compileUnit, nextCur);
        }
        return *this;
    }

    Die::ChildrenRange::iterator Die::ChildrenRange::iterator::operator++(int)
    {
        auto t = *this;
        ++(*this);
        return t;
    }

    FileAddr Attr::AsAddress() const
    {
        Cursor cursor{{location, compileUnit->Data().End()}};
        if (form != DW_FORM_addr) Error::Send("Invalid address type");
        auto elf = compileUnit->DwarfInfo()->ElfFile();
        return {*elf, cursor.U64()};
    }

    std::uint32_t Attr::AsSectionOffset() const
    {
        Cursor cursor{{location, compileUnit->Data().End()}};
        if (form != DW_FORM_sec_offset) Error::Send("Invalid offset type");
        // only support 32bit
        return cursor.U32();
    }

    Span<const std::byte> Attr::AsBlock() const
    {
        Cursor cursor{{location, compileUnit->Data().End()}};
        std::size_t size;
        switch (form)
        {
            case DW_FORM_block1:
                size = cursor.U8();
                break;
            case DW_FORM_block2:
                size = cursor.U16();
                break;
            case DW_FORM_block4:
                size = cursor.U32();
                break;
            case DW_FORM_block:
                size = cursor.Uleb128();
                break;
            default:
                Error::Send("Invalid block type");
        }
        return {cursor.Position(), size};
    }

    std::uint64_t Attr::AsInt() const
    {
        Cursor cursor{{location, compileUnit->Data().End()}};
        switch (form)
        {
            case DW_FORM_data1:
                return cursor.U8();
            case DW_FORM_data2:
                return cursor.U16();
            case DW_FORM_data4:
                return cursor.U32();
            case DW_FORM_data8:
                return cursor.U64();
            case DW_FORM_udata:
                return cursor.Uleb128();
            default:
                Error::Send("Invalid integer type");
        }
    }

    std::string_view Attr::AsString() const
    {
        Cursor cursor{{location, compileUnit->Data().End()}};
        switch (form)
        {
            case DW_FORM_string:
                return cursor.String();
            case DW_FORM_strp:
            {
                auto offset = cursor.U32();
                auto section = compileUnit->DwarfInfo()->ElfFile()->GetSectionContents(".debug_str");
                Cursor cursor{{section.Begin() + offset, section.End()}};
                return cursor.String();
            }
            default:
                Error::Send("Invalid string type");
        }
    }

    Die Attr::AsReference() const
    {
        Cursor cursor{{location, compileUnit->Data().End()}};
        std::size_t offset;
        switch (form)
        {
            case DW_FORM_ref1:
                offset = cursor.U8();
                break;
            case DW_FORM_ref2:
                offset = cursor.U16();
                break;
            case DW_FORM_ref4:
                offset = cursor.U32();
                break;
            case DW_FORM_ref8:
                offset = cursor.U64();
                break;
            case DW_FORM_ref_udata:
                offset = cursor.Uleb128();
                break;
            case DW_FORM_ref_addr:
                // can reference data in other compile units
                // so its offset is relative to the start of `.debug_info` section
                {
                    offset = cursor.U32();
                    auto section = compileUnit->DwarfInfo()->ElfFile()->GetSectionContents(".debug_info");
                    auto diePos = section.Begin() + offset;
                    auto& compileUnits = compileUnit->DwarfInfo()->CompileUnits();
                    auto belongs = std::ranges::find_if(compileUnits, [diePos](const auto& cu)
                    {
                        return cu->Data().Begin() <= diePos && diePos < cu->Data().End();
                    });
                    Cursor refCursor{{diePos, belongs->get()->Data().End()}};
                    return ParseDie(*belongs->get(), refCursor);
                }
            default:
                Error::Send("Invalid reference type");
        }
        Cursor refCursor{{compileUnit->Data().Begin()+ offset, compileUnit->Data().End()}};
        return ParseDie(*compileUnit, refCursor);
    }
}
