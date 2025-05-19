#pragma once

#include <elf.h>

#include <cstddef>
#include <filesystem>
#include <libldb/types.hpp>
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

    private:
        /// @brief 解析 section header table
        void parse_section_headers();

        /// @brief 构建 section 名称到 section header 的映射
        void build_section_headers_map();

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
    };
} // namespace ldb
