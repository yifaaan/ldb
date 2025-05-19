#pragma once

#include <elf.h>

#include <cstddef>
#include <filesystem>
#include <libldb/types.hpp>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

namespace ldb
{
    class elf
    {
    public:
        explicit elf(std::filesystem::path path);
        ~elf();

        elf(const elf&) = delete;
        elf& operator=(const elf&) = delete;
        elf(elf&&) = delete;
        elf& operator=(elf&&) = delete;

        /// @brief 获取文件路径
        const std::filesystem::path& path() const
        {
            return path_;
        }

        /// @brief 获取 ELF 头
        const Elf64_Ehdr& header() const
        {
            return header_;
        }

        /// @brief 获取 section 名称
        /// @param index section名称在 shstrtab节中的索引
        /// @return section 名称
        std::string_view get_section_name(std::size_t index) const;

        /// @brief 获取 section header
        /// @param name section 名称
        /// @return section header
        std::optional<const Elf64_Shdr*> get_section_header(std::string_view name) const;

        /// @brief 获取 section 内容
        /// @param name section 名称
        /// @return section 内容
        span<const std::byte> get_section_contents(std::string_view name) const;

        /// @brief 获取字符串
        /// @param index 字符串在 strtab 或 dynstr 中的索引
        /// @return 字符串
        std::string_view get_string(std::size_t index) const;

        /// @brief 获取加载偏移
        /// @return 加载偏移
        virt_addr load_bias() const
        {
            return load_bias_;
        }

        /// @brief 通知加载，更新加载偏移
        /// @param address 加载偏移
        void notify_loaded(virt_addr address)
        {
            load_bias_ = address;
        }

        /// @brief 获取包含指定地址的 section
        /// @param addr 文件地址
        /// @return section
        const Elf64_Shdr* get_section_containing_address(file_addr addr) const;

        /// @brief 获取包含指定地址的 section
        /// @param addr 虚拟地址
        /// @return section
        const Elf64_Shdr* get_section_containing_address(virt_addr addr) const;

        /// @brief 获取 section 起始文件地址
        /// @param name section 名称
        /// @return section 起始文件地址
        std::optional<file_addr> get_section_start_file_address(std::string_view name) const;

        /// @brief 获取符号
        /// @param name 符号名称
        /// @return 符号
        std::vector<const Elf64_Sym*> get_symbols_by_name(std::string_view name) const;

        /// @brief 获取指定地址的符号
        /// @param addr 文件地址
        /// @return 符号
        std::optional<const Elf64_Sym*> get_symbol_at_address(file_addr addr) const;

        /// @brief 获取指定地址的符号
        /// @param addr 虚拟地址
        /// @return 符号
        std::optional<const Elf64_Sym*> get_symbol_at_address(virt_addr addr) const;

        /// @brief 获取包含指定地址的符号
        /// @param addr 文件地址
        /// @return 符号
        std::optional<const Elf64_Sym*> get_symbol_containing_address(file_addr addr) const;

        /// @brief 获取包含指定地址的符号
        /// @param addr 虚拟地址
        /// @return 符号
        std::optional<const Elf64_Sym*> get_symbol_containing_address(virt_addr addr) const;

    private:
        /// @brief 解析 section header table
        void parse_section_headers();

        /// @brief 构建 section 名称到 section header 的映射
        void build_section_headers_map();

        /// @brief 解析符号表
        void parse_symbol_table();

        /// @brief 构建符号表名称到符号的映射
        void build_symbol_maps();

        static constexpr auto range_comparator = [](const std::pair<file_addr, file_addr>& lhs, const std::pair<file_addr, file_addr>& rhs)
        {
            return lhs.first < rhs.first;
        };

        /// @brief 文件描述符
        int fd_ = -1;
        /// @brief 文件路径
        std::filesystem::path path_;
        /// @brief 文件大小
        std::size_t file_size_ = 0;
        /// @brief 文件数据
        std::byte* data_ = nullptr;
        /// @brief ELF 头
        Elf64_Ehdr header_;
        /// @brief section header table
        std::vector<Elf64_Shdr> section_headers_;
        /// @brief section 名称到 section header 的映射
        std::unordered_map<std::string_view, Elf64_Shdr*> section_headers_map_;
        /// @brief 加载偏移
        virt_addr load_bias_;
        /// @brief 符号表
        std::vector<Elf64_Sym> symbol_table_;
        /// @brief 符号表名称到符号的映射
        std::unordered_multimap<std::string_view, Elf64_Sym*> symbol_name_map_;
        /// @brief 符号表地址范围到符号的映射
        std::map<std::pair<file_addr, file_addr>, Elf64_Sym*, decltype(range_comparator)> symbol_address_map_;
    };
} // namespace ldb
