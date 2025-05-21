#pragma once

#include <cstdint>
#include <libldb/types.hpp>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace ldb
{
    /// @brief abbrev table 条目中的属性规范
    struct attr_spec
    {
        /// @brief 属性
        std::uint64_t attr;
        /// @brief 形式，表示该属性值的编码方式（所占字节数等）
        std::uint64_t form;
    };

    /// @brief abbrev table 条目
    struct abbrev
    {
        /// @brief 缩写码
        std::uint64_t code;
        /// @brief 标签
        std::uint64_t tag;
        /// @brief 是否存在子节点
        bool has_children;
        /// @brief 属性规范列表
        std::vector<attr_spec> attr_specs;
    };

    class compile_unit;
    /// @brief 范围列表，表示非连续地址范围.debug_ranges节中的条目表
    class range_list
    {
    public:
        range_list(const compile_unit* cu, span<const std::byte> data, file_addr base_address)
            : compile_unit_{cu}
            , data_{data}
            , base_address_{base_address}
        {
        }

        /// @brief 范围列表条目, 表示一个连续的地址范围
        struct entry
        {
            file_addr low;
            file_addr high;
            bool contains(file_addr addr) const
            {
                return low <= addr && addr < high;
            }
        };

        class iterator;
        iterator begin() const;
        iterator end() const;

        bool contains(file_addr addr) const;

    private:
        /// @brief 所属的编译单元
        const compile_unit* compile_unit_ = nullptr;
        /// @brief 范围列表数据
        span<const std::byte> data_;
        /// @brief 基地址
        file_addr base_address_;
    };

    class compile_unit;
    class die;
    /// @brief 属性
    class attr
    {
    public:
        attr(const compile_unit* cu, std::uint64_t type, std::uint64_t form, const std::byte* location)
            : compile_unit_{cu}
            , type_{type}
            , form_{form}
            , location_{location}
        {
        }

        /// @brief 获取属性类型
        std::uint64_t name() const
        {
            return type_;
        }

        /// @brief 获取属性形式
        std::uint64_t form() const
        {
            return form_;
        }

        /*解析属性值*/

        /// @brief 属性值->file_addr
        file_addr as_address() const;

        /// @brief 属性值->section_offset
        std::uint32_t as_section_offset() const;

        /// @brief 属性值->block
        ldb::span<const std::byte> as_block() const;

        /// @brief 属性值->int
        std::uint64_t as_int() const;

        /// @brief 属性值->string
        std::string_view as_string() const;

        /// @brief 表示引用编译单元的某个DIE
        die as_reference() const;

        /// @brief 表示.debug_ranges节中的范围列表
        range_list as_range_list() const;

    private:
        /// @brief 所属的编译单元
        const compile_unit* compile_unit_;
        /// @brief 类型
        std::uint64_t type_;
        /// @brief 形式
        std::uint64_t form_;
        /// @brief 属性值所在位置
        const std::byte* location_;
    };

    class range_list::iterator
    {
    public:
        using value_type = entry;
        using reference = const value_type&;
        using pointer = const value_type*;
        using difference_type = std::ptrdiff_t;
        using iterator_category = std::forward_iterator_tag;

        iterator() = default;
        iterator(const iterator&) = default;
        iterator& operator=(const iterator&) = default;
        iterator(iterator&&) = default;
        iterator& operator=(iterator&&) = default;

        iterator(const compile_unit* cu, span<const std::byte> data, file_addr base_address);

        reference operator*() const
        {
            return current_;
        }

        pointer operator->() const
        {
            return &current_;
        }

        bool operator==(const iterator& other) const
        {
            return position_ == other.position_;
        }

        bool operator!=(const iterator& other) const
        {
            return !(*this == other);
        }

        iterator& operator++();
        iterator operator++(int);

    private:
        const compile_unit* compile_unit_ = nullptr;
        span<const std::byte> data_;
        file_addr base_address_;
        const std::byte* position_ = nullptr;
        entry current_;
    };

    class dwarf;
    class die;
    /// @brief 编译单元
    class compile_unit
    {
    public:
        /// @brief 构造编译单元
        /// @param parent 所属的 dwarf
        /// @param data 编译单元的数据,包含header
        /// @param abbrev_offset 编译单元对应的abbrev_table在.debug_abbrev节中的偏移量
        compile_unit(dwarf& parent, span<const std::byte> data, std::size_t abbrev_offset)
            : parent_{&parent}
            , data_{data}
            , abbrev_offset_{abbrev_offset}
        {
        }

        /// @brief 所属的 dwarf
        const dwarf* dwarf_info() const
        {
            return parent_;
        }

        /// @brief 编译单元的数据
        span<const std::byte> data() const
        {
            return data_;
        }

        /// @brief 编译单元的abbrev_table
        /// @note 键为缩写码，值为abbrev_table条目
        const std::unordered_map<std::uint64_t, abbrev>& abbrev_table() const;

        /// @brief 编译单元的根节点
        die root() const;

    private:
        /// @brief 所属的 dwarf
        dwarf* parent_ = nullptr;
        /// @brief 编译单元的数据,包含header
        span<const std::byte> data_;
        /// @brief 编译单元对应的abbrev_table在.debug_abbrev节中的偏移量
        std::size_t abbrev_offset_ = 0;
    };

    /// @brief 调试信息条目
    class die
    {
    public:
        /// @brief 构造DIE
        /// @param next 下一个DIE的位置
        explicit die(const std::byte* next)
            : next_{next}
        {
        }

        /// @brief 构造DIE
        /// @param position DIE在.debug_info节中对应编译单元的起始位置
        /// @param parent 所属的编译单元
        /// @param abbrev 对应在abbrev_table中的条目
        /// @param next 下一个DIE
        /// @param attr_locations 属性位置缓存列表
        die(const std::byte* position, const compile_unit* parent, const abbrev* abbrev, const std::byte* next, std::vector<const std::byte*> attr_locations)
            : position_{position}
            , compile_unit_{parent}
            , abbrev_{abbrev}
            , next_{next}
            , attr_locations_{std::move(attr_locations)}
        {
        }

        class children_range;
        /// @brief 迭代子DIE
        children_range children() const;

        /// @brief 获取abbrev_table中的条目
        const abbrev* abbrev_entry() const
        {
            return abbrev_;
        }

        /// @brief 获取下一个DIE
        const std::byte* next() const
        {
            return next_;
        }

        /// @brief 是否包含指定属性
        /// @param attribute 属性, 例如 DW_AT_name
        /// @return 是否包含
        bool contains(std::uint64_t attribute) const;

        /// @brief 获取指定属性值
        /// @param attribute 属性, 例如 DW_AT_name
        /// @return 属性值
        attr operator[](std::uint64_t attribute) const;

        /// @brief 获取低地址
        file_addr low_pc() const;

        /// @brief 获取高地址
        file_addr high_pc() const;

        /// @brief 是否包含指定地址
        /// @param addr 文件地址
        /// @return 是否包含
        bool contains_address(file_addr addr) const;

    private:
        /// @brief DIE起始位置
        const std::byte* position_ = nullptr;
        /// @brief 所属的编译单元
        const compile_unit* compile_unit_ = nullptr;
        /// @brief 对应在abbrev_table中的条目
        const abbrev* abbrev_ = nullptr;
        /// @brief 下一个DIE
        const std::byte* next_ = nullptr;
        /// @brief 属性值位置缓存列表
        std::vector<const std::byte*> attr_locations_;
    };

    class die::children_range
    {
    public:
        explicit children_range(die parent)
            : die_{std::move(parent)}
        {
        }

        class iterator
        {
        public:
            using value_type = die;
            using reference = const value_type&;
            using pointer = const value_type*;
            using difference_type = std::ptrdiff_t;
            using iterator_category = std::forward_iterator_tag;

            iterator() = default;
            iterator(const iterator&) = default;
            iterator& operator=(const iterator&) = default;
            iterator(iterator&&) = default;
            iterator& operator=(iterator&&) = default;

            /// @brief 构造子DIE迭代器
            /// @param parent 父DIE
            explicit iterator(const die& parent);

            const die& operator*() const
            {
                return *die_;
            }

            const die* operator->() const
            {
                return &*die_;
            }

            iterator& operator++();
            iterator operator++(int);

            bool operator==(const iterator& other) const;
            bool operator!=(const iterator& other) const
            {
                return !(*this == other);
            }

        private:
            std::optional<die> die_;
        };

        iterator begin() const
        {
            if (die_.abbrev_->has_children)
            {
                return iterator{die_};
            }
            return end();
        }

        iterator end() const
        {
            return {};
        }

    private:
        die die_;
    };

    class elf;
    /// @brief dwarf调试信息
    class dwarf
    {
    public:
        explicit dwarf(const elf& parent)
            : elf_{&parent}
        {
        }

        /// @brief 所属的 elf 文件
        const elf* elf_file() const
        {
            return elf_;
        }

        /// @brief 获取abbrev_table
        /// @param offset abbrev_table在.debug_abbrev节中的偏移量
        /// @return abbrev_table
        const std::unordered_map<std::uint64_t, abbrev>& get_abbrev_table(std::size_t offset);

        /// @brief 获取所有编译单元
        /// @return 所有编译单元
        const std::vector<std::unique_ptr<compile_unit>>& compile_units() const
        {
            return compile_units_;
        }

    private:
        /// @brief 所属的 elf 文件
        const elf* elf_ = nullptr;
        /// @brief 所有编译单元的缩写表
        /// @note 键为相对于.debug_abbrev节的偏移量，值为该编译单元的缩写表
        std::unordered_map<std::size_t, std::unordered_map<std::uint64_t, abbrev>> abbrev_tables_;

        /// @brief .dubug_info节中所有编译单元
        std::vector<std::unique_ptr<compile_unit>> compile_units_;
    };
} // namespace ldb
