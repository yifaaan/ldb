#include "libldb/detail/dwarf.h"
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
}
