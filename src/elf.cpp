#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

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
