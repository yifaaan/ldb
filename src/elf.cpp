#include <cxxabi.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <iostream>
#include <libldb/bit.hpp>
#include <libldb/elf.hpp>
#include <libldb/error.hpp>

ldb::elf::elf(std::filesystem::path path)
    : path_{std::move(path)}
{
    if (fd_ = ::open(path_.c_str(), O_RDONLY | O_LARGEFILE); fd_ < 0)
    {
        error::send_errno("Could not open elf file");
    }
    struct stat stats;
    if (::fstat(fd_, &stats) < 0)
    {
        ::close(fd_);
        error::send_errno("Could not get elf file stats");
    }
    file_size_ = stats.st_size;
    auto mmapped_mem = ::mmap(nullptr, file_size_, PROT_READ, MAP_SHARED, fd_, 0);
    if (mmapped_mem == MAP_FAILED)
    {
        ::close(fd_);
        error::send_errno("Could not mmap elf file");
    }
    data_ = reinterpret_cast<std::byte*>(mmapped_mem);
    std::copy(data_, data_ + sizeof(Elf64_Ehdr), as_bytes(header_));

    // 解析 section header table
    parse_section_headers();
    // 构建 section 名称到 section header 的映射
    build_section_headers_map();
    // 解析符号表
    parse_symbol_table();
    // 构建符号表名称到符号的映射
    build_symbol_maps();
    // 构建 dwarf
    dwarf_ = std::make_unique<dwarf>(*this);
}

ldb::elf::~elf()
{
    ::munmap(data_, file_size_);
    ::close(fd_);
}

void ldb::elf::parse_section_headers()
{
    // ELF 头部 (Elf64_Ehdr) 中的 e_shoff 字段给出了节头表（Section Header Table）在文件中的字节偏移。
    // 也就是说，文件开头 + e_shoff 就是首个 Elf64_Shdr 条目的地址。
    std::size_t n_headers = header_.e_shnum;
    // 当节数量 ≥ 0xff00 时，e_shnum 字段将被置为 0，真正的节数量会存放在第 0 个节头的 sh_size 字段
    if (n_headers == 0 && header_.e_shentsize != 0)
    {
        n_headers = from_bytes<Elf64_Shdr>(data_ + header_.e_shoff).sh_size;
    }
    section_headers_.resize(n_headers);
    std::copy(data_ + header_.e_shoff, data_ + header_.e_shoff + n_headers * sizeof(Elf64_Shdr), reinterpret_cast<std::byte*>(section_headers_.data()));
}

std::string_view ldb::elf::get_section_name(std::size_t index) const
{
    // shstrtab 节头的索引
    auto& section_header = section_headers_[header_.e_shstrndx];
    return {reinterpret_cast<char*>(data_) + section_header.sh_offset + index};
}

void ldb::elf::build_section_headers_map()
{
    for (auto& header : section_headers_)
    {
        section_headers_map_[get_section_name(header.sh_name)] = &header;
    }
}

std::optional<const Elf64_Shdr*> ldb::elf::get_section_header(std::string_view name) const
{
    if (section_headers_map_.contains(name))
    {
        return section_headers_map_.at(name);
    }
    return std::nullopt;
}

ldb::span<const std::byte> ldb::elf::get_section_contents(std::string_view name) const
{
    if (auto header = get_section_header(name); header)
    {
        return {reinterpret_cast<const std::byte*>(data_ + header.value()->sh_offset), header.value()->sh_size};
    }
    return {};
}

std::string_view ldb::elf::get_string(std::size_t index) const
{
    // 有些 ELF 文件可能只有一个精简版的 `.dynstr` 节来代替 `.strtab` 节
    auto opt_header = get_section_header(".strtab");
    if (!opt_header)
    {
        opt_header = get_section_header(".dynstr");
        if (!opt_header)
        {
            return "";
        }
    }
    return {reinterpret_cast<char*>(data_) + opt_header.value()->sh_offset + index};
}

const Elf64_Shdr* ldb::elf::get_section_containing_address(file_addr addr) const
{
    if (addr.elf_file() != this)
    {
        return nullptr;
    }
    // 遍历所有 section header，找到包含指定地址的 section
    for (auto& header : section_headers_)
    {
        // elf中的地址为文件地址，虚拟地址为加载偏移 + 文件地址
        if (header.sh_addr <= addr.addr() && addr.addr() < header.sh_addr + header.sh_size)
        {
            return &header;
        }
    }
    return nullptr;
}

const Elf64_Shdr* ldb::elf::get_section_containing_address(virt_addr addr) const
{
    for (auto& header : section_headers_)
    {
        if (load_bias_ + header.sh_addr <= addr && addr <= load_bias_ + header.sh_addr + header.sh_size)
        {
            return &header;
        }
    }
    return nullptr;
}

std::optional<ldb::file_addr> ldb::elf::get_section_start_file_address(std::string_view name) const
{
    if (auto header = get_section_header(name); header)
    {
        return file_addr{*this, header.value()->sh_offset};
    }
    return std::nullopt;
}

void ldb::elf::parse_symbol_table()
{
    // 获取符号表节头，.symtab 或 .dynsym
    auto opt_header = get_section_header(".symtab");
    if (!opt_header)
    {
        opt_header = get_section_header(".dynsym");
        if (!opt_header)
        {
            return;
        }
    }
    const Elf64_Shdr* symtab_header = *opt_header;
    // 计算符号数量：节的总大小 / 单个符号条目的大小
    symbol_table_.resize(symtab_header->sh_size / symtab_header->sh_entsize);
    std::copy(data_ + symtab_header->sh_offset, data_ + symtab_header->sh_offset + symtab_header->sh_size, reinterpret_cast<std::byte*>(symbol_table_.data()));
}

void ldb::elf::build_symbol_maps()
{
    for (auto& symbol : symbol_table_)
    {
        // 获取混淆名称
        auto mangled_name = get_string(symbol.st_name);

        // demangled
        int demangle_status = -1;
        auto demangle_c_str = abi::__cxa_demangle(mangled_name.data(), nullptr, nullptr, &demangle_status);
        if (demangle_status == 0 && demangle_c_str)
        {
            auto demangled_name = std::string_view{demangle_c_str};
            symbol_name_map_.emplace(demangled_name, &symbol);
            // std::cout << "symbol.st_name: " << mangled_name << " -> " << demangled_name << std::endl;
            free(demangle_c_str);
        }

        symbol_name_map_.emplace(mangled_name, &symbol);
        // 如果符号有地址、有名称，并且不是线程局部存储 (TLS) 符号
        if (symbol.st_value != 0 && symbol.st_name != 0 && ELF64_ST_TYPE(symbol.st_info) != STT_TLS)
        {
            // 计算符号的文件地址范围
            file_addr start_addr{*this, symbol.st_value};
            file_addr end_addr = start_addr + symbol.st_size;
            symbol_address_map_.emplace(std::make_pair(start_addr, end_addr), &symbol);
        }
    }
}

std::vector<const Elf64_Sym*> ldb::elf::get_symbols_by_name(std::string_view name) const
{
    auto [begin, end] = symbol_name_map_.equal_range(name);
    std::vector<const Elf64_Sym*> symbols;
    symbols.reserve(std::distance(begin, end));
    std::ranges::transform(begin,
                           end,
                           std::back_inserter(symbols),
                           [](const auto& pair)
                           {
                               return pair.second;
                           });
    return symbols;
}

std::optional<const Elf64_Sym*> ldb::elf::get_symbol_at_address(file_addr addr) const
{
    if (addr.elf_file() != this)
    {
        return std::nullopt;
    }
    file_addr dummy_end_addr;
    auto key_to_find = std::make_pair(addr, dummy_end_addr);
    if (auto it = symbol_address_map_.find(key_to_find); it != symbol_address_map_.end())
    {
        return it->second;
    }
    return std::nullopt;
}

std::optional<const Elf64_Sym*> ldb::elf::get_symbol_at_address(virt_addr addr) const
{
    return get_symbol_at_address(addr.to_file_addr(*this));
}

std::optional<const Elf64_Sym*> ldb::elf::get_symbol_containing_address(file_addr addr) const
{
    if (addr.elf_file() != this || symbol_address_map_.empty())
    {
        return std::nullopt;
    }
    file_addr dummy_end_addr;
    auto key_for_lower_bound = std::make_pair(addr, dummy_end_addr);
    // 找第一个key>=addr的
    auto it = symbol_address_map_.lower_bound(key_for_lower_bound);
    if (it != symbol_address_map_.end())
    {
        if (auto [key, value] = *it; key.first == addr)
        {
            return value;
        }
    }
    if (it == symbol_address_map_.begin())
    {
        return std::nullopt;
    }
    --it;
    if (auto [key, value] = *it; key.first < addr && addr < key.second)
    {
        return value;
    }
    return std::nullopt;
}

std::optional<const Elf64_Sym*> ldb::elf::get_symbol_containing_address(virt_addr addr) const
{
    return get_symbol_containing_address(addr.to_file_addr(*this));
}
