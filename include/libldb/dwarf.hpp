#pragma once

#include <cstdint>
#include <filesystem>
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

    class line_table
    {
    public:
        struct entry;
        struct file
        {
            std::filesystem::path name;
            std::uint64_t modification_time;
            std::uint64_t length;
        };

        class iterator;
        iterator begin() const;
        iterator end() const;

        line_table() = delete;
        line_table(const line_table&) = delete;
        line_table& operator=(const line_table&) = delete;
        line_table(line_table&&) = default;
        line_table& operator=(line_table&&) = default;
        ~line_table() = default;

        line_table(span<const std::byte> data,
                   const compile_unit* cu,
                   bool default_is_stmt,
                   std::int8_t line_base,
                   std::uint8_t line_range,
                   std::uint8_t opcode_base,
                   std::vector<std::filesystem::path> include_directories,
                   std::vector<file> file_names)
            : data_{data}
            , compile_unit_{cu}
            , default_is_stmt_{default_is_stmt}
            , line_base_{line_base}
            , line_range_{line_range}
            , opcode_base_{opcode_base}
            , include_directories_{std::move(include_directories)}
            , file_names_{std::move(file_names)}
        {
        }

        const compile_unit* cu() const
        {
            return compile_unit_;
        }

        const std::vector<file>& file_names() const
        {
            return file_names_;
        }

    private:
        span<const std::byte> data_;
        const compile_unit* compile_unit_ = nullptr;
        bool default_is_stmt_ = false;
        std::int8_t line_base_ = 0;
        std::uint8_t line_range_ = 0;
        /// @brief 标准操作码的编号从 1 到 opcode_base-1
        std::uint8_t opcode_base_ = 0;
        /// @brief 包含目录
        std::vector<std::filesystem::path> include_directories_;
        /// @brief 涉及的源文件
        mutable std::vector<file> file_names_;
    };

    /// @brief 行号表寄存器
    struct line_table::entry
    {
        file_addr address;
        /// @brief file_names_的索引
        std::uint64_t file_index = 1;
        /// @brief 行号
        std::uint64_t line = 1;
        /// @brief 列号
        std::uint64_t column = 0;
        /// @brief 是否是语句开始
        bool is_stmt = false;
        /// @brief 是否是子程序
        bool basic_block_start = false;
        /// @brief 是否是指令序列末尾的下一个地址
        bool end_sequence = false;
        /// @brief 是否是函数序言的结束
        bool prologue_end = false;
        /// @brief 是否是函数尾声的开始
        bool epilogue_begin = false;
        /// @brief 是否是函数序言的结束
        bool epilogue_end = false;
        /// @brief 区分符
        std::uint64_t discriminator = 0;

        file* file_entry = nullptr;

        bool operator==(const entry& other) const
        {
            return address == other.address && file_index == other.file_index && line == other.line && column == other.column &&
            discriminator == other.discriminator;
        }
    };

    class line_table::iterator
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

        explicit iterator(const line_table* table);

        const entry& operator*() const
        {
            return current_;
        }

        const entry* operator->() const
        {
            return &current_;
        }

        iterator& operator++();
        iterator operator++(int);

        bool operator==(const iterator& other) const
        {
            return position_ == other.position_;
        }

        bool operator!=(const iterator& other) const
        {
            return !(*this == other);
        }

    private:
        /// @brief 执行单条指令
        /// @return 是否应发出新的矩阵行
        bool execute_instruction();

        const line_table* table_ = nullptr;
        line_table::entry current_;
        line_table::entry registers_;
        const std::byte* position_ = nullptr;
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
        compile_unit(dwarf& parent, span<const std::byte> data, std::size_t abbrev_offset);

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

        /// @brief 行号表
        const line_table& lines() const
        {
            return *line_table_;
        }

    private:
        /// @brief 所属的 dwarf
        dwarf* parent_ = nullptr;
        /// @brief 编译单元的数据,包含header
        span<const std::byte> data_;
        /// @brief 编译单元对应的abbrev_table在.debug_abbrev节中的偏移量
        std::size_t abbrev_offset_ = 0;
        /// @brief 行号表
        std::unique_ptr<line_table> line_table_;
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

        /// @brief 所属的编译单元
        const compile_unit* cu() const
        {
            return compile_unit_;
        }

        /// @brief DIE在.debug_info节中的位置
        const std::byte* position() const
        {
            return position_;
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

        /// @brief 获取DIE的名称
        std::optional<std::string_view> name() const;

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
        explicit dwarf(const elf& parent);

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

        /// @brief 获取包含指定地址的编译单元
        /// @param address 文件地址
        /// @return 包含指定地址的编译单元
        const compile_unit* compile_unit_containing_address(file_addr address) const;

        /// @brief 获取包含指定地址的函数的DIE
        /// @param address 文件地址
        /// @return 包含指定地址的函数的DIE
        std::optional<die> function_containing_address(file_addr address) const;

        /// @brief 获取所有包含指定名称的函数DIE
        /// @param name 函数名称
        /// @return 所有包含指定名称的函数DIE
        std::vector<die> find_functions(std::string name) const;

    private:
        /// @brief 索引所有编译单元的所有DIE
        void index() const;

        /// @brief 索引当前DIE及子DIE
        /// @param current 当前DIE
        void index_die(const die& current) const;

        /// @brief DIE索引条目
        struct index_entry
        {
            /// @brief 所属的编译单元
            const compile_unit* cu;
            /// @brief 位置
            const std::byte* position;
        };

        /// @brief 所属的 elf 文件
        const elf* elf_ = nullptr;
        /// @brief 所有编译单元的缩写表
        /// @note 键为相对于.debug_abbrev节的偏移量，值为该编译单元的缩写表
        std::unordered_map<std::size_t, std::unordered_map<std::uint64_t, abbrev>> abbrev_tables_;

        /// @brief .dubug_info节中所有编译单元
        std::vector<std::unique_ptr<compile_unit>> compile_units_;

        /// @brief 函数索引
        mutable std::unordered_multimap<std::string, index_entry> function_index_;
    };
} // namespace ldb
