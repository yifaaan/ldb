#include <string_view>
#include <algorithm>

#include <libldb/elf.hpp>
#include <libldb/bit.hpp>
#include <libldb/dwarf.hpp>
#include <libldb/types.hpp>
#include <libldb/error.hpp>

namespace
{
    class Cursor
    {
    public:
        explicit Cursor(ldb::Span<const std::byte> _data)
            :data{ _data }
            ,pos{ _data.Begin() }
        {}
    
        Cursor& operator++() { ++pos; return *this; }

        Cursor& operator+=(std::size_t n) { pos += n; return *this; }

        const std::byte* Position() const { return pos; }

        bool Finished() const { return pos >= data.End(); }


        template<typename T>
        T FixedInt()
        {
            auto t = ldb::FromBytes<T>(pos);
            pos += sizeof(T);
            return t;
        }

        std::uint8_t U8() { return FixedInt<std::uint8_t>(); }

        std::uint16_t U16() { return FixedInt<std::uint16_t>(); }

        std::uint32_t U32() { return FixedInt<std::uint32_t>(); }

        std::uint64_t U64() { return FixedInt<std::uint64_t>(); }

        std::int8_t S8() { return FixedInt<std::int8_t>(); }

        std::int16_t S16() { return FixedInt<std::int16_t>(); }

        std::int32_t S32() { return FixedInt<std::int32_t>(); }

        std::int64_t S64() { return FixedInt<std::int64_t>(); }

        std::string_view String()
        {
            auto nullTerminator = std::find(pos, data.End(), std::byte{0});
            auto ret = std::string_view(reinterpret_cast<const char*>(pos), static_cast<std::size_t>(nullTerminator - pos));
            pos = nullTerminator + 1;
            return ret;
        }

        /// 读取ULEB128编码的整数
        ///
        /// ULEB128(无符号)编码步骤：
        ///
        /// 将整数转为二进制,填充到7的倍数位
        /// 每7位分为一组,除最后一组外，每组前加1位标志位：
        ///
        /// 最高组加0,其他组加1
        /// 示例：编码`624,485`
        ///
        /// 624,485的二进制表示为：
        ///
        /// 0000 0000 0000 0000 0001 0011 0001 0000 0000 1101
        ///
        /// 填充到7的倍数位：
        /// 0000 0000 0000 0000 0001 0011 0001 0000 0000 1101
        /// 
        /// 每7位分为一组：
        /// 0000 000 0000 0000 0001 001 1000 1000 0000 0110 1
        ///
        /// 每组前加1位标志位：
        /// 0000 000 0000 0000 0001 001 1000 1000 0000 0110 1
        ///
        /// 最高组加0,其他组加1：
        /// 0000 000 0000 0000 0001 001 1000 1000 0000 0110 1
        std::uint64_t Uleb128()
        {
            // 小端序处理
            std::uint64_t result = 0;
            int shift = 0;
            std::uint8_t byte = 0;
            while (true)
            {
                byte = U8();
                result |= static_cast<std::uint64_t>(byte & 0x7F) << shift;
                shift += 7;
                if (!(byte & 0x80))
                {
                    break;
                }
            }
            return result;
        }

        /// 读取SLEB128编码的整数
        /// 
        /// SLEB128(有符号)编码步骤：
        ///
        /// 将整数转为二进制,填充到7的倍数位
        /// 每7位分为一组,除最后一组（最高位）外，每组前加1位标志位：
        std::int64_t Sleb128()
        {
            std::int64_t result = 0;  // 存储最终结果
            int shift = 0;            // 位移计数器
            std::uint8_t byte = 0;    // 当前读取的字节
    
            while (true)
            {
                // 读取一个字节
                byte = U8();  
                // 取出低7位并左移到正确的位置
                result |= static_cast<std::int64_t>(byte & 0x7F) << shift;
                shift += 7;   // 更新位移量
                
                // 如果最高位是0，表示这是最后一个字节
                if (!(byte & 0x80))
                {
                    break;
                }
            }

            // 处理符号位扩展
            if (shift < 64 && (byte & 0x40))
            {
                // 进行符号位扩展，将高位都填充为1
                result |= ~static_cast<std::int64_t>(0) << shift;
            }
            return result;
        }

        void SkipForm(std::uint64_t form)
        {
            switch (form)
            {
            // TODO: many
            default: ldb::Error::Send("Unrecongnized DWARF form");
            }
        }

    private:
        ldb::Span<const std::byte> data;
        const std::byte* pos;
    };

    std::unordered_map<std::uint64_t, ldb::Abbrev> ParseAbbrevTable(const ldb::Elf& elf, std::size_t offset)
    {
        Cursor cursor{ elf.GetSectionContents(".debug_abbrev") };
        cursor += offset;
        // 每个表项的格式为（encoded as an ULEB128 integer）:
        // [缩写码] [标签-对应dwarf.h中的DW_TAG_常量] [子DIE标志位] [属性规格列表-类型，形式] [终止符]
        std::unordered_map<std::uint64_t, ldb::Abbrev> table;
        std::uint64_t code = 0;
        while (true)
        {
            code = cursor.Uleb128();
            auto tag = cursor.Uleb128();
            auto hasChildren = cursor.U8() != 0;
            
            std::vector<ldb::AttrSpec> attrSpecs;
            while (true)
            {
                auto attr = cursor.Uleb128();
                auto form = cursor.Uleb128();
                if (attr != 0)
                {
                    attrSpecs.emplace_back(attr, form);
                }
                else
                {
                    break;
                }
            }

            if (code != 0)
            {
                table.emplace(code, ldb::Abbrev{ code, tag, hasChildren, std::move(attrSpecs) });
            }
            else
            {
                break;
            }
        }
        return table;
    }

    ///.`debug_info
    ///
    // Compile Unit 1
    //   Header
    //     unit_length - 单元长度
    //     version - DWARF版本
    //     debug_abbrev_offset - 缩写表偏移
    //     address_size - 地址大小
    //   DIE条目们...
    //
    // Compile Unit 2
    //   Header
    //   DIE条目们...
    //
    // ...更多编译单元
    std::unique_ptr<ldb::CompileUnit> ParseCompileUnit(ldb::Dwarf& dwarf, const ldb::Elf& elf, Cursor cursor)
    {
        auto start = cursor.Position();
        // parse header
        auto unitLength = cursor.U32();
        auto version = cursor.U16();
        auto abbrevOffset = cursor.U32();
        auto addressSize = cursor.U8();

        if (unitLength == 0xffffffff)
        {
            ldb::Error::Send("Only DWARF32 is supported");
        }
        if (version != 4)
        {
            ldb::Error::Send("Only DWARF version 4 is supported");
        }
        if (addressSize != 8)
        {
            ldb::Error::Send("Only 64-bit address size is supported");
        }

        // 编译单元结构：
        // ┌─────────────┬────────┬──────────────────┬────────────┬─────────────┐
        // │ unitLength  │version │   abbrevOffset   │ addressSize│   DIE数据    │
        // │  (4字节)     │(2字节) │    (4字节)        │  (1字节)   │   (变长)     │
        // └─────────────┴────────┴──────────────────┴────────────┴─────────────┘
        // unitLength字段存储的值不包括自己的大小
        unitLength += sizeof(std::uint32_t);

        ldb::Span<const std::byte> data = { start, unitLength };
        return std::make_unique<ldb::CompileUnit>(dwarf, data, abbrevOffset);
    }

    /// 解析`.debug_info`段中的编译单元
    ///
    /// 编译单元的格式为：
    ///
    /// [版本] [单位头长度] [单位头] [单位体]
    std::vector<std::unique_ptr<ldb::CompileUnit>> ParseCompileUnits(ldb::Dwarf& dwarf, const ldb::Elf& elf)
    {
        auto debugInfo = elf.GetSectionContents(".debug_info");
        Cursor cursor{ debugInfo };

        std::vector<std::unique_ptr<ldb::CompileUnit>> compileUnits;
        while (!cursor.Finished())
        {
            auto compileUnit = ParseCompileUnit(dwarf, elf, cursor);
            cursor += compileUnit->Data().Size();
            compileUnits.emplace_back(std::move(compileUnit));
        }
        return compileUnits;
    }

    /// 解析DIE条目
    ///
    /// ┌──────────────┬──────────────┬────────────┬────────────┬─────────────┐
    /// │ Abbrev Code  │    Tag       │ HasChildren│ Attributes │    ...      │
    /// │              │  (隐含的)     │  (隐含的)   │            │             │
    /// └──────────────┴──────────────┴────────────┴────────────┴─────────────┘
    ldb::Die ParseDie(const ldb::CompileUnit& cu, Cursor cursor)
    {
        auto pos = cursor.Position();
        auto abbrevCode = cursor.Uleb128();
        // code为0表示这是一个空条目，用于标记兄弟节点链的结束
        if (abbrevCode == 0)
        {
            auto next = cursor.Position();
            return ldb::Die{ next };
        }
        auto& abbrevTable = cu.AbbrevTable();
        auto& abbrev = abbrevTable.at(abbrevCode);
        std::vector<const std::byte*> attrLocs;
        attrLocs.reserve(abbrev.attrSpecs.size());
        for (const auto& attrSpec : abbrev.attrSpecs)
        {
            attrLocs.emplace_back(cursor.Position());
            cursor.SkipForm(attrSpec.form);
        }
        auto next = cursor.Position();
        return ldb::Die{ pos, &cu, &abbrev, std::move(attrLocs), next };
    }
}

ldb::Dwarf::Dwarf(const Elf& elf)
    :elf{ &elf }
{
    compileUnits = ParseCompileUnits(*this, elf);
}


const std::unordered_map<std::uint64_t, ldb::Abbrev>& ldb::Dwarf::GetAbbrevTable(std::size_t offset)
{
    if (!abbrevTables.contains(offset))
    {
        abbrevTables.emplace(offset, ParseAbbrevTable(*elf, offset));
    }
    return abbrevTables.at(offset);
}

const std::unordered_map<std::uint64_t, ldb::Abbrev>& ldb::CompileUnit::AbbrevTable() const
{
    return parent->GetAbbrevTable(abbrevOffset);
}

ldb::Die ldb::CompileUnit::Root() const
{
    std::size_t headerSize = 11;
    // 跳过header
    Cursor cursor{ {data.Begin() + headerSize, data.End() } };
    return ParseDie(*this, cursor);
}