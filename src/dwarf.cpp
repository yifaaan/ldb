#include <algorithm>

#include <libldb/dwarf.hpp>
#include <libldb/types.hpp>
#include <libldb/bit.hpp>
#include <libldb/elf.hpp>

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
}

namespace ldb
{
    const std::unordered_map<std::uint64_t, Abbrev>& Dwarf::GetAbbrevTable(std::size_t offset)
    {
        if (!abbrevTables.contains(offset))
        {
            abbrevTables.emplace(offset, ParseAbbrevTable(*elf, offset));
        }
        return abbrevTables.at(offset);
    }    


}
