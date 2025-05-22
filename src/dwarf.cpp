#include <fmt/base.h>
#include <libldb/detail/dwarf.h>

#include <algorithm>
#include <libldb/bit.hpp>
#include <libldb/dwarf.hpp>
#include <libldb/elf.hpp>
#include <libldb/error.hpp>
#include <libldb/types.hpp>

namespace
{
    class cursor
    {
    public:
        explicit cursor(ldb::span<const std::byte> data)
            : data_{data}
            , current_{data_.begin()}
        {
        }

        /// @brief 移动到下一个字节
        cursor& operator++()
        {
            ++current_;
            return *this;
        }

        /// @brief 移动指定字节数
        cursor& operator+=(std::size_t size)
        {
            current_ += size;
            return *this;
        }

        /// @brief 获取当前位置
        const std::byte* position() const
        {
            return current_;
        }

        /// @brief 是否已经到达末尾
        bool finished() const
        {
            return current_ >= data_.end();
        }

        template <typename T>
            requires std::is_integral_v<T>
        T fixed_int()
        {
            auto ret = ldb::from_bytes<T>(current_);
            current_ += sizeof(T);
            return ret;
        }

        std::uint8_t u8()
        {
            return fixed_int<std::uint8_t>();
        }

        std::uint16_t u16()
        {
            return fixed_int<std::uint16_t>();
        }

        std::uint32_t u32()
        {
            return fixed_int<std::uint32_t>();
        }

        std::uint64_t u64()
        {
            return fixed_int<std::uint64_t>();
        }

        std::int8_t s8()
        {
            return fixed_int<std::int8_t>();
        }

        std::int16_t s16()
        {
            return fixed_int<std::int16_t>();
        }

        std::int32_t s32()
        {
            return fixed_int<std::int32_t>();
        }

        std::int64_t s64()
        {
            return fixed_int<std::int64_t>();
        }

        std::string_view string()
        {
            auto null_pos = std::ranges::find(current_, data_.end(), std::byte{0});
            std::string_view ret{reinterpret_cast<const char*>(current_), static_cast<std::size_t>(null_pos - current_)};
            current_ = null_pos + 1;
            return ret;
        }

        std::uint64_t uleb128()
        {
            std::uint64_t ret = 0;
            std::uint64_t shift = 0;
            std::uint8_t byte;
            do
            {
                byte = u8();
                // 低7位
                auto mask = static_cast<std::uint64_t>(byte) & 0x7f;
                ret |= mask << shift;
                shift += 7;
            }
            while ((byte & 0x80) != 0);
            return ret;
        }

        std::uint64_t sleb128()
        {
            std::uint64_t ret = 0;
            std::uint64_t shift = 0;
            std::uint8_t byte;
            do
            {
                byte = u8();
                // 低7位
                auto mask = static_cast<std::uint64_t>(byte) & 0x7f;
                ret |= mask << shift;
                shift += 7;
            }
            while ((byte & 0x80) != 0);

            // 符号位
            if (shift < sizeof(ret) * 8 && (byte & 0x40) != 0)
            {
                ret |= (~static_cast<std::uint64_t>(0)) << shift;
            }
            return ret;
        }

        /// @brief 根据属性form跳过适当的字节数，当解析 DIE 的属性时，我们从缩写声明中知道每个属性的编码格式，
        /// 因此可以跳过适当的字节数，以跳到下一个属性值。
        void skip_form(std::uint64_t form)
        {
            switch (form)
            {
            case DW_FORM_flag_present: // 0 bytes
                break;
            case DW_FORM_data1: // 1 byte
            case DW_FORM_ref1:  // 1 byte
            case DW_FORM_flag:  // 1 byte (0 or 1)
                current_ += 1;
                break;
            case DW_FORM_data2: // 2 bytes
            case DW_FORM_ref2:  // 2 bytes
                current_ += 2;
                break;
            case DW_FORM_data4:      // 4 bytes
            case DW_FORM_ref4:       // 4 bytes
            case DW_FORM_ref_addr:   // 4 bytes (offset in .debug_info)
            case DW_FORM_sec_offset: // 4 bytes (offset in other sections)
            case DW_FORM_strp:       // 4 bytes (offset in .debug_str for 32-bit DWARF)
                                     // (Note: for DWARF64, strp and sec_offset can be 8 bytes,
                                     //  but book focuses on DWARF32 form for these in this context)
                current_ += 4;
                break;
            case DW_FORM_data8: // 8 bytes
            case DW_FORM_addr:  // 8 bytes (on x64, address size)
                current_ += 8;
                break;
            case DW_FORM_sdata:
                sleb128();
                break; // Call sleb128() to parse and advance cursor
            case DW_FORM_udata:
            case DW_FORM_ref_udata:
                uleb128();
                break; // Call uleb128() to parse and advance cursor

            case DW_FORM_block1:
                // DW_FORM_block1: 属性值以一个1字节无符号整数开始，表示后续数据块的长度。
                // u8() 会读取这个1字节的长度值，并使 pos_ 前进1字节。
                // 然后我们将 pos_ 再前进刚读取到的长度值。
                current_ += u8(); // 读取1字节长度，并按此长度跳过数据块
                break;
            case DW_FORM_block2:
                // DW_FORM_block2: 属性值以一个2字节无符号整数开始，表示后续数据块的长度。
                current_ += u16(); // 读取2字节长度，并按此长度跳过数据块
                break;
            case DW_FORM_block4:
                // DW_FORM_block4: 属性值以一个4字节无符号整数开始，表示后续数据块的长度。
                current_ += u32(); // 读取4字节长度，并按此长度跳过数据块
                break;
            case DW_FORM_block:   // 对于通用的 DW_FORM_block
            case DW_FORM_exprloc: // DWARF 位置表达式
                // 这两种形式都以一个 ULEB128 编码的整数开始，该整数表示后续数据块的长度。
                // uleb128() 会读取这个变长的长度值，并使 pos_ 前进相应的字节数。
                // 然后我们将 pos_ 再前进刚读取到的长度值。
                current_ += uleb128(); // 读取ULEB128长度，并按此长度跳过数据块
                break;
            case DW_FORM_string:
                // DW_FORM_string 表示一个以空字符 ('\0') 结尾的 C 风格字符串，
                // 直接内联在 .debug_info 中。
                while (!finished() && *current_ != std::byte{0})
                {               // 检查是否到达数据末尾，以及当前字节是否为空
                    ++current_; // 如果不是空字节，则游标前进
                }
                // 此时，pos_ 指向空终止符，或者如果字符串未正确终止则指向数据末尾。
                // 跳过这个空终止符本身
                if (!finished())
                {
                    ++current_; // 跳过空终止符
                }
                break;
            case DW_FORM_indirect:
                {
                    // DW_FORM_indirect: 属性值首先是一个 ULEB128 编码的整数，
                    // 这个整数的值是该属性 *实际* 的形式代码 (例如，它可能是 DW_FORM_data4)。
                    // 然后才跟着按那个实际形式编码的属性值。
                    // 所以，我们先用 uleb128() 读取出这个实际的形式代码，
                    // 然后递归地调用 skip_form 来跳过按那个实际形式编码的数据。
                    auto form = uleb128();
                    skip_form(form);
                    break;
                }
            default:
                ldb::error::send("Unrecognized DWARF form");
            }
        }

    private:
        ldb::span<const std::byte> data_;
        const std::byte* current_;
    };

    std::unordered_map<std::uint64_t, ldb::abbrev> parse_abbrev_table(const ldb::elf& elf, std::size_t offset)
    {
        std::unordered_map<std::uint64_t, ldb::abbrev> ret;
        cursor cur{elf.get_section_contents(".debug_abbrev")};
        // 定位到对应的表
        cur += offset;
        std::uint64_t code = 0;
        do
        {
            // 读取缩写码
            code = cur.uleb128();
            // 读取标签
            auto tag = cur.uleb128();
            // 读取是否存在子节点
            auto has_children = cur.u8() != 0;
            // 读取属性规范列表
            std::vector<ldb::attr_spec> attr_specs;
            std::uint64_t attr = 0;
            do
            {
                // 读取属性
                attr = cur.uleb128();
                // 读取形式
                auto form = cur.uleb128();
                if (attr != 0)
                {
                    attr_specs.emplace_back(attr, form);
                }
            }
            while (attr != 0);

            if (code != 0)
            {
                ret.emplace(code, ldb::abbrev{.code = code, .tag = tag, .has_children = has_children, .attr_specs = std::move(attr_specs)});
            }
        }
        while (code != 0);
        return ret;
    }

    /// @brief 解析编译单元
    /// @param dwarf 所属的 dwarf
    /// @param obj 所属的 elf 文件
    /// @param cur 编译单元所在位置
    /// @return 编译单元
    std::unique_ptr<ldb::compile_unit> parse_compile_unit(ldb::dwarf& dwarf, const ldb::elf& obj, cursor cur)
    {
        auto start = cur.position();
        // 表示从紧随其后的字段开始，到这个编译单元数据结束的总字节数。这个长度不包括 size 字段本身的大小
        auto size = cur.u32();
        // 版本号
        auto version = cur.u16();
        // 缩写表的偏移量
        auto abbrev_offset = cur.u32();
        // 地址大小
        auto address_size = cur.u8();

        if (size == 0xffffffff)
        {
            ldb::error::send("Only DWARF32 is supported");
        }
        if (version != 4)
        {
            ldb::error::send("Only DWARF4 is supported");
        }
        if (address_size != 8)
        {
            ldb::error::send("Invalid address size");
        }
        size += sizeof(size);
        ldb::span<const std::byte> data{start, size};
        return std::make_unique<ldb::compile_unit>(dwarf, data, abbrev_offset);
    }

    /// @brief 解析所有编译单元
    /// @param dwarf 所属的 dwarf
    /// @param obj 所属的 elf 文件
    /// @return 所有编译单元
    std::vector<std::unique_ptr<ldb::compile_unit>> parse_compile_units(ldb::dwarf& dwarf, const ldb::elf& obj)
    {
        std::vector<std::unique_ptr<ldb::compile_unit>> ret;
        cursor cur{obj.get_section_contents(".debug_info")};
        while (!cur.finished())
        {
            auto unit = parse_compile_unit(dwarf, obj, cur);
            cur += unit->data().size();
            ret.emplace_back(std::move(unit));
        }
        return ret;
    }

    ldb::die parse_die(const ldb::compile_unit& unit, cursor& cur)
    {
        auto start = cur.position();
        // DIE 本身只存储一个指向缩写表条目的索引，以及该 DIE 的所有属性值
        auto abbrev_code = cur.uleb128();
        if (abbrev_code == 0)
        {
            // 空DIE
            auto next_die_pos = cur.position();
            return ldb::die{next_die_pos};
        }
        // 根据abbrev_code获取该die对应的abbrev table的条目
        auto& abbrev_table = unit.abbrev_table();
        auto& abbrev = abbrev_table.at(abbrev_code);
        std::vector<const std::byte*> attr_locations;
        // 一共abbrev.attr_specs.size()个属性
        attr_locations.reserve(abbrev.attr_specs.size());
        // 遍历属性规格列表
        for (auto& attr_spec : abbrev.attr_specs)
        {
            attr_locations.emplace_back(cur.position());
            // skip_form 会根据属性的形式代码 (attribute_specification.form)
            // 读取并前进游标 cur 适当的字节数，以跳到下一个属性值
            cur.skip_form(attr_spec.form);
        }
        // 由于跳过了该DIE的所有属性值，接着就是下一个DIE的位置
        auto next_die_pos = cur.position();
        return ldb::die{start, &unit, &abbrev, next_die_pos, std::move(attr_locations)};
    }

    ldb::line_table::file parse_line_table_file(cursor& cur,
                                                const std::filesystem::path& compile_dir,
                                                const std::vector<std::filesystem::path>& include_directories)
    {
        // 可以是完整路径、相对路径或仅仅是文件名
        auto file_name = cur.string();
        // 指向——include_directories中的一个条目。0表示文件名本身已经是完整路径，或者该文件位于编译单元的编译目录（由 DW_AT_comp_dir 指定）
        auto dir_index = cur.uleb128();
        auto last_modified = cur.uleb128();
        auto file_length = cur.uleb128();
        std::filesystem::path path{file_name};
        if (!file_name.starts_with("/"))
        {
            if (dir_index == 0)
            {
                path = compile_dir / path;
            }
            else
            {
                path = include_directories[dir_index - 1] / path;
            }
        }
        return {.name = path, .modification_time = last_modified, .length = file_length};
    }

    /// DWARF 4 标准操作码的操作数数量
    constexpr std::array<std::uint8_t, 12> expected_opcode_lengths = {0, 1, 1, 1, 1, 0, 0, 0, 1, 0, 0, 1};
    std::unique_ptr<ldb::line_table> parse_line_table(const ldb::compile_unit& unit)
    {
        auto section = unit.dwarf_info()->elf_file()->get_section_contents(".debug_line");
        // `DW_AT_stmt_list` 的值是一个指向 `.debug_line` 节（section）开头的偏移量（offset)
        if (!unit.root().contains(DW_AT_stmt_list))
        {
            return nullptr;
        }
        auto offset = unit.root()[DW_AT_stmt_list].as_section_offset();
        cursor cur{{section.begin() + offset, section.end()}};

        // unit_length, 行号表大小不包括unit_length字段本身
        auto line_table_length = cur.u32();
        auto end = cur.position() + line_table_length;
        // version
        auto version = cur.u16();
        if (version != 4)
        {
            ldb::error::send("Only DWARF4 is supported");
        }
        // header_length
        cur.u32();
        // min_inst_length must 1 in x64
        auto min_inst_length = cur.u8();
        if (min_inst_length != 1)
        {
            ldb::error::send("min_inst_length must 1 in x64");
        }
        // maximum_operations_per_instruction
        auto max_ops_per_inst = cur.u8();
        if (max_ops_per_inst != 1)
        {
            ldb::error::send("maximum_operations_per_instruction must 1 in x64");
        }
        // default_is_stmt
        auto default_is_stmt = cur.u8();
        // line_base
        auto line_base = cur.s8();
        // line_range
        auto line_range = cur.u8();
        // opcode_base，标准操作码的编号从 1 到 opcode_base-1
        auto opcode_base = cur.u8();
        // standard_opcode_lengths，标准操作码对应的操作数数量
        for (int i = 0; i < opcode_base - 1; i++)
        {
            if (cur.u8() != expected_opcode_lengths[i])
            {
                ldb::error::send("Invalid standard opcode length");
            }
        }
        // include_directories，包括相对路径和绝对路径
        std::vector<std::filesystem::path> include_directories;
        std::filesystem::path compile_dir{unit.root()[DW_AT_comp_dir].as_string()};
        for (auto dir = cur.string(); !dir.empty(); dir = cur.string())
        {
            if (dir.starts_with("/"))
            {
                include_directories.emplace_back(dir);
            }
            else
            {
                include_directories.emplace_back(compile_dir / dir);
            }
        }
        std::vector<ldb::line_table::file> file_names;
        while (*cur.position() != std::byte{0})
        {
            file_names.emplace_back(parse_line_table_file(cur, compile_dir, include_directories));
        }
        cur += 1;
        ldb::span<const std::byte> data{cur.position(), end};
        return std::make_unique<ldb::line_table>(
        data, &unit, default_is_stmt, line_base, line_range, opcode_base, std::move(include_directories), std::move(file_names));
    }
} // namespace

const std::unordered_map<std::uint64_t, ldb::abbrev>& ldb::dwarf::get_abbrev_table(std::size_t offset)
{
    if (!abbrev_tables_.contains(offset))
    {
        abbrev_tables_.emplace(offset, parse_abbrev_table(*elf_, offset));
    }
    return abbrev_tables_.at(offset);
}

const std::unordered_map<std::uint64_t, ldb::abbrev>& ldb::compile_unit::abbrev_table() const
{
    return parent_->get_abbrev_table(abbrev_offset_);
}

ldb::dwarf::dwarf(const elf& parent)
    : elf_{&parent}
{
    compile_units_ = parse_compile_units(*this, parent);
}

ldb::die ldb::compile_unit::root() const
{
    auto header_size = 11;
    // 不包括header信息，从第一个die开始
    cursor cur{{data_.begin() + header_size, data_.end()}};
    return parse_die(*this, cur);
}

ldb::die::children_range::iterator::iterator(const die& parent)
{
    // 解析parent的next
    cursor child_cursor{{parent.next_, parent.compile_unit_->data().end()}};
    die_ = parse_die(*parent.compile_unit_, child_cursor);
}

bool ldb::die::children_range::iterator::operator==(const iterator& other) const
{
    bool lhs_null = !die_.has_value() || !die_->abbrev_entry();
    bool rhs_null = !other.die_.has_value() || !other.die_->abbrev_entry();
    if (lhs_null && rhs_null)
    {
        return true;
    }
    if (lhs_null || rhs_null)
    {
        return false;
    }
    return die_->abbrev_entry() == other.die_->abbrev_entry() && die_->next() == other.die_->next();
}

ldb::die::children_range::iterator& ldb::die::children_range::iterator::operator++()
{
    if (!die_.has_value() || !die_->abbrev_entry())
    {
        return *this;
    }
    if (!die_->abbrev_entry()->has_children)
    {
        // 没有子DIE，下一个就是兄弟DIE，直接parse
        cursor next_cursor{{die_->next(), die_->compile_unit_->data().end()}};
        die_ = parse_die(*die_->compile_unit_, next_cursor);
    }
    else if (die_->contains(DW_AT_sibling))
    {
        // 有兄弟DIE，直接parse
        die_ = die_.value()[DW_AT_sibling].as_reference();
    }
    else
    {
        // 有子DIE，需要跳过所有儿子
        iterator child_iter{*die_};
        while (child_iter->abbrev_entry())
        {
            ++child_iter;
        }
        // 跳过所有儿子后，下一个就是兄弟DIE，直接parse
        cursor next_cursor{{child_iter->next(), die_->compile_unit_->data().end()}};
        die_ = parse_die(*die_->compile_unit_, next_cursor);
    }
    return *this;
}

ldb::die::children_range::iterator ldb::die::children_range::iterator::operator++(int)
{
    auto tmp = *this;
    ++(*this);
    return tmp;
}

ldb::die::children_range ldb::die::children() const
{
    return children_range{*this};
}

bool ldb::die::contains(std::uint64_t attribute) const
{
    auto& attr_specs = abbrev_->attr_specs;
    return std::ranges::any_of(attr_specs,
                               [attribute](auto& attr_spec)
                               {
                                   return attr_spec.attr == attribute;
                               });
}

ldb::attr ldb::die::operator[](std::uint64_t attribute) const
{
    if (!abbrev_)
    {
        error::send("Cannot access attributes: DIE has no abbrev entry");
    }
    auto& attr_specs = abbrev_->attr_specs;
    for (int i = 0; i < attr_specs.size(); ++i)
    {
        if (attr_specs[i].attr == attribute)
        {
            return {compile_unit_, attribute, attr_specs[i].form, attr_locations_[i]};
        }
    }
    error::send("Attribute not found");
}

ldb::file_addr ldb::attr::as_address() const
{
    cursor cur{{location_, compile_unit_->data().end()}};
    if (form_ != DW_FORM_addr)
    {
        error::send("Invalid form for address attribute");
    }
    auto elf = compile_unit_->dwarf_info()->elf_file();
    return file_addr{*elf, cur.u64()};
}

std::uint32_t ldb::attr::as_section_offset() const
{
    cursor cur{{location_, compile_unit_->data().end()}};
    if (form_ != DW_FORM_sec_offset)
    {
        error::send("Invalid form for section offset attribute");
    }
    return cur.u32();
}

std::uint64_t ldb::attr::as_int() const
{
    cursor cur{{location_, compile_unit_->data().end()}};
    switch (form_)
    {
    case DW_FORM_data1:
        return cur.u8();
    case DW_FORM_data2:
        return cur.u16();
    case DW_FORM_data4:
        return cur.u32();
    case DW_FORM_data8:
        return cur.u64();
    case DW_FORM_udata:
        return cur.uleb128();
    default:
        error::send("Invalid form for integer attribute");
    }
}

ldb::span<const std::byte> ldb::attr::as_block() const
{
    std::size_t size = 0;
    cursor cur{{location_, compile_unit_->data().end()}};

    // -----------------
    // |  size  | data |
    // ----------------
    // 首先获取块的大小
    switch (form_)
    {
    case DW_FORM_block1:
        size = cur.u8();
        break;
    case DW_FORM_block2:
        size = cur.u16();
        break;
    case DW_FORM_block4:
        size = cur.u32();
        break;
    case DW_FORM_block:
        size = cur.uleb128();
        break;
    default:
        error::send("Invalid form for block attribute");
    }
    // 然后获取块的内容
    return {cur.position(), size};
}

ldb::die ldb::attr::as_reference() const
{
    cursor cur{{location_, compile_unit_->data().end()}};
    // 相对于编译单元的偏移量
    std::uint64_t offset = 0;
    switch (form_)
    {
    case DW_FORM_ref1:
        offset = cur.u8();
        break;
    case DW_FORM_ref2:
        offset = cur.u16();
        break;
    case DW_FORM_ref4:
        offset = cur.u32();
        break;
    case DW_FORM_ref8:
        offset = cur.u64();
        break;
    case DW_FORM_ref_udata:
        offset = cur.uleb128();
        break;
    case DW_FORM_ref_addr:
        {
            // offset 是相对于整个 .debug_info 节起始的4字节偏移 (对于32位DWARF)
            offset = cur.u32();
            auto debug_info_content_span = compile_unit_->dwarf_info()->elf_file()->get_section_contents(".debug_info");
            auto die_start_ptr = debug_info_content_span.begin() + offset;
            // 计算该地址属于哪个编译单元
            auto& compile_units_vec = compile_unit_->dwarf_info()->compile_units();
            auto target_cu = std::ranges::find_if(compile_units_vec,
                                                  [die_start_ptr](const auto& cu_ptr)
                                                  {
                                                      return cu_ptr->data().begin() <= die_start_ptr && die_start_ptr < cu_ptr->data().end();
                                                  });
            if (target_cu == compile_units_vec.end())
            {
                error::send("DW_FORM_ref_addr: Referenced DIE does not belong to any known compile unit");
            }
            cursor ref_cursor{{die_start_ptr, (*target_cu)->data().end()}};
            return parse_die(**target_cu, ref_cursor);
        }
    default:
        error::send("Invalid form for reference attribute");
    }
    cursor ref_cursor{{compile_unit_->data().begin() + offset, compile_unit_->data().end()}};
    return parse_die(*compile_unit_, ref_cursor);
}

std::string_view ldb::attr::as_string() const
{
    cursor cur{{location_, compile_unit_->data().end()}};
    switch (form_)
    {
    case DW_FORM_string:
        return cur.string();
    case DW_FORM_strp:
        {
            // 属性值是一个指向 .debug_str 节的偏移量
            auto offset = cur.u32();
            auto debug_str_content_span = compile_unit_->dwarf_info()->elf_file()->get_section_contents(".debug_str");
            if (offset >= debug_str_content_span.size())
            {
                error::send("DW_FORM_strp: Invalid string offset");
            }
            cursor str_cursor{{debug_str_content_span.begin() + offset, debug_str_content_span.end()}};
            return str_cursor.string();
        }
    default:
        error::send("Invalid form for string attribute");
    }
}

ldb::file_addr ldb::die::low_pc() const
{
    if (contains(DW_AT_ranges))
    {
        // 如果die包含DW_AT_ranges属性，则该DIE对应多个地址范围,起始地址为第一个范围的低地址
        return (*this)[DW_AT_ranges].as_range_list().begin()->low;
    }
    else if (contains(DW_AT_low_pc))
    {
        // low_pc只有DW_FORM_addr形式
        return (*this)[DW_AT_low_pc].as_address();
    }
    error::send("DW_AT_low_pc attribute not found");
}

ldb::file_addr ldb::die::high_pc() const
{
    if (contains(DW_AT_ranges))
    {
        // 如果die包含DW_AT_ranges属性，则该DIE对应多个地址范围,结束地址为最后一个范围的高地址
        auto range_list = (*this)[DW_AT_ranges].as_range_list();
        auto it = range_list.begin();
        while (std::next(it) != range_list.end())
        {
            ++it;
        }
        return it->high;
    }
    else if (contains(DW_AT_high_pc))
    {
        auto attr = (*this)[DW_AT_high_pc];
        ldb::file_addr addr;
        if (attr.form() == DW_FORM_addr)
        {
            // 直接地址 (DW_FORM_addr)
            addr = attr.as_address();
        }
        else
        {
            // 偏移量
            addr = low_pc() + attr.as_int();
        }
        return addr;
    }
    error::send("DW_AT_high_pc attribute not found");
}

ldb::range_list::iterator::iterator(const compile_unit* cu, span<const std::byte> data, file_addr base_address)
    : compile_unit_{cu}
    , data_{data}
    , base_address_{base_address}
    , position_{data.begin()}
{
    ++(*this);
}

ldb::range_list::iterator& ldb::range_list::iterator::operator++()
{
    auto elf = compile_unit_->dwarf_info()->elf_file();
    constexpr auto base_address_flag = ~static_cast<std::uint64_t>(0);
    cursor cur{{position_, data_.end()}};
    while (true)
    {
        current_.low = file_addr{*elf, cur.u64()};
        current_.high = file_addr{*elf, cur.u64()};

        if (current_.low.addr() == base_address_flag)
        {
            // 基地址选择器
            base_address_ = current_.high;
        }
        else if (current_.low.addr() == 0 && current_.high.addr() == 0)
        {
            // 列表结束指示器
            position_ = nullptr;
            break;
        }
        else
        {
            // 常规条目
            position_ = cur.position();
            current_.low += base_address_.addr();
            current_.high += base_address_.addr();
            break;
        }
    }
    return *this;
}

ldb::range_list::iterator ldb::range_list::iterator::operator++(int)
{
    auto tmp = *this;
    ++(*this);
    return tmp;
}

ldb::range_list ldb::attr::as_range_list() const
{
    auto section = compile_unit_->dwarf_info()->elf_file()->get_section_contents(".debug_ranges");
    auto offset = as_section_offset();
    span<const std::byte> data{section.begin() + offset, section.end()};
    // root die中是否有DW_AT_low_pc属性，如果有则使用该属性值作为基地址，否则使用0作为基地址
    auto root = compile_unit_->root();
    file_addr base_address = root.contains(DW_AT_low_pc) ? root[DW_AT_low_pc].as_address() : file_addr{};
    return {compile_unit_, data, base_address};
}

ldb::range_list::iterator ldb::range_list::begin() const
{
    return {compile_unit_, data_, base_address_};
}

ldb::range_list::iterator ldb::range_list::end() const
{
    return {};
}

bool ldb::range_list::contains(file_addr addr) const
{
    return std::ranges::any_of(begin(),
                               end(),
                               [addr](const auto& entry)
                               {
                                   return entry.contains(addr);
                               });
}

bool ldb::die::contains_address(file_addr addr) const
{
    if (addr.elf_file() != compile_unit_->dwarf_info()->elf_file())
    {
        return false;
    }
    if (contains(DW_AT_ranges))
    {
        // 如果die包含DW_AT_ranges属性，则该DIE对应多个地址范围，需要检查所有范围
        return (*this)[DW_AT_ranges].as_range_list().contains(addr);
    }
    else if (contains(DW_AT_low_pc))
    {
        // 如果die包含DW_AT_low_pc属性，则该DIE对应一个地址范围，检查该范围是否包含指定地址
        return low_pc() <= addr && addr < high_pc();
    }
    return false;
}

const ldb::compile_unit* ldb::dwarf::compile_unit_containing_address(file_addr address) const
{
    auto it = std::ranges::find_if(compile_units_,
                                   [address](const auto& cu_ptr)
                                   {
                                       return cu_ptr->root().contains_address(address);
                                   });
    if (it == compile_units_.end())
    {
        return nullptr;
    }
    return it->get();
}

std::optional<ldb::die> ldb::dwarf::function_containing_address(file_addr address) const
{
    index();
    for (const auto& [name, entry] : function_index_)
    {
        cursor cur{{entry.position, entry.cu->data().end()}};
        auto die = parse_die(*entry.cu, cur);
        // 如果DIE包含指定地址，并且DIE的标签为DW_TAG_subprogram（非内联），则返回该DIE
        if (die.contains_address(address) && die.abbrev_entry()->tag == DW_TAG_subprogram)
        {
            return die;
        }
    }
    return std::nullopt;
}

std::vector<ldb::die> ldb::dwarf::find_functions(std::string name) const
{
    index();
    std::vector<die> ret;
    auto [begin, end] = function_index_.equal_range(name);
    std::ranges::transform(begin,
                           end,
                           std::back_inserter(ret),
                           [](const auto& pair)
                           {
                               const auto [name, entry] = pair;
                               cursor cur{{entry.position, entry.cu->data().end()}};
                               return parse_die(*entry.cu, cur);
                           });
    return ret;
}

void ldb::dwarf::index() const
{
    if (!function_index_.empty())
    {
        return;
    }

    std::ranges::for_each(compile_units_,
                          [this](const auto& cu_ptr)
                          {
                              auto root = cu_ptr->root();
                              index_die(root);
                          });
}

std::optional<std::string_view> ldb::die::name() const
{
    if (contains(DW_AT_name))
    {
        return (*this)[DW_AT_name].as_string();
    }
    if (contains(DW_AT_specification))
    {
        // 表示声明，需要通过间接引用获取名称
        return (*this)[DW_AT_specification].as_reference().name();
    }
    if (contains(DW_AT_abstract_origin))
    {
        // 表示内联函数的一个实例，需要通过间接引用获取原始函数
        return (*this)[DW_AT_abstract_origin].as_reference().name();
    }
    return std::nullopt;
}

void ldb::dwarf::index_die(const ldb::die& current) const
{
    bool has_range = current.contains(DW_AT_ranges) || current.contains(DW_AT_low_pc);
    bool is_function = current.abbrev_entry()->tag == DW_TAG_subprogram || current.abbrev_entry()->tag == DW_TAG_inlined_subroutine;
    if (has_range && is_function)
    {
        if (auto name = current.name(); name)
        {
            index_entry entry{.cu = current.cu(), .position = current.position()};
            function_index_.emplace(*name, entry);
        }
    }
    for (auto& child : current.children())
    {
        index_die(child);
    }
}

ldb::compile_unit::compile_unit(dwarf& parent, span<const std::byte> data, std::size_t abbrev_offset)
    : parent_{&parent}
    , data_{data}
    , abbrev_offset_{abbrev_offset}
{
    line_table_ = parse_line_table(*this);
}

ldb::line_table::iterator::iterator(const line_table* table)
    : table_{table}
    , position_{table->data_.begin()}
{
    registers_.is_stmt = table_->default_is_stmt_;
    ++(*this);
}

ldb::line_table::iterator ldb::line_table::begin() const
{
    return iterator{this};
}

ldb::line_table::iterator ldb::line_table::end() const
{
    return {};
}

ldb::line_table::iterator& ldb::line_table::iterator::operator++()
{
    if (position_ == table_->data_.end())
    {
        position_ = nullptr;
        return *this;
    }
    bool emit_row = false;
    do
    {
        emit_row = execute_instruction();
    }
    while (!emit_row);
    current_.file_entry = &table_->file_names_[current_.file_index - 1];
    return *this;
}

ldb::line_table::iterator ldb::line_table::iterator::operator++(int)
{
    auto tmp = *this;
    ++(*this);
    return tmp;
}

bool ldb::line_table::iterator::execute_instruction()
{
    auto elf = table_->compile_unit_->dwarf_info()->elf_file();
    cursor cur{{position_, table_->data_.end()}};
    auto opcode = cur.u8();
    bool emit_row = false;
    if (opcode > 0 && opcode < table_->opcode_base_)
    {
        // 标准操作码
        switch (opcode)
        {
        case DW_LNS_copy:
            // 发出矩阵行
            current_ = registers_;
            registers_.basic_block_start = false;
            registers_.prologue_end = false;
            registers_.epilogue_begin = false;
            registers_.discriminator = 0;
            emit_row = true;
            break;
        case DW_LNS_advance_pc:
            // 推进PC
            registers_.address += cur.uleb128();
            break;
        case DW_LNS_advance_line:
            // 推进行号
            registers_.line += cur.sleb128();
            break;
        case DW_LNS_set_file:
            // 设置文件
            current_.file_index = cur.uleb128();
            break;
        case DW_LNS_set_column:
            // 设置列
            current_.column = cur.uleb128();
            break;
        case DW_LNS_negate_stmt:
            registers_.is_stmt = !registers_.is_stmt;
            break;
        case DW_LNS_set_basic_block:
            // 设置基本块
            registers_.basic_block_start = true;
            break;
        case DW_LNS_const_add_pc:
            registers_.address += (255 - table_->opcode_base_) / table_->line_range_;
            break;
        case DW_LNS_fixed_advance_pc:
            // 固定推进PC
            registers_.address += cur.u16();
            break;
        case DW_LNS_set_prologue_end:
            // 设置函数序言结束
            registers_.prologue_end = true;
            break;
        case DW_LNS_set_epilogue_begin:
            // 设置函数尾声开始
            registers_.epilogue_begin = true;
            break;
        case DW_LNS_set_isa:
            break;
        default:
            error::send("unknown standard opcode");
        }
    }
    else if (opcode == 0)
    {
        // 扩展操作码

        // 指令长度
        auto length = cur.uleb128();
        // 扩展操作码
        auto extended_opcode = cur.u8();
        switch (extended_opcode)
        {
        case DW_LNE_end_sequence:
            // 结束序列，并发出矩阵行
            registers_.end_sequence = true;
            current_ = registers_;
            registers_ = entry{};
            registers_.is_stmt = table_->default_is_stmt_;
            emit_row = true;
            break;
        case DW_LNE_set_address:
            // 设置地址
            registers_.address = file_addr{*elf, cur.u64()};
            break;
        case DW_LNE_define_file:
            {
                auto compile_dir = table_->compile_unit_->root()[DW_AT_comp_dir].as_string();
                auto file = parse_line_table_file(cur, compile_dir, table_->include_directories_);
                table_->file_names_.push_back(file);
                break;
            }
        case DW_LNE_set_discriminator:
            // 设置区分符
            registers_.discriminator = cur.uleb128();
            break;
        default:
            error::send("unknown extended opcode");
        }
    }
    else
    {
        error::send("unknown opcode");
    }

    position_ = cur.position();
    return emit_row;
}
