
#include <elf.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

#include <libldb/elf.hpp>
#include <libldb/error.hpp>
#include <libldb/bit.hpp>




ldb::Elf::Elf(const std::filesystem::path& _path)
    :path(_path)
{
    if ((fd = open(path.c_str(), O_LARGEFILE, O_RDONLY)) < 0)
    {
        Error::SendErrno("Could not open ELF file");
    }

    struct stat stats;
    if (fstat(fd, &stats) < 0)
    {
        Error::SendErrno("Could not retrieve ELF file stats");
    }
    fileSize = stats.st_size;

    void* ret;
    if ((ret = mmap(0, fileSize, PROT_READ, MAP_SHARED, fd, 0)) == MAP_FAILED)
    {
        close(fd);
        Error::SendErrno("Could not mmap ELF file");
    }
    data = reinterpret_cast<std::byte*>(ret);
    std::copy(data, data + sizeof(header), AsBytes(header));

    BuildSectionMap();
}

ldb::Elf::~Elf()
{
    munmap(data, fileSize);
    close(fd);
}


void ldb::Elf::ParseSectionHeaders()
{
    auto nHeaders = header.e_shnum;
    if (nHeaders == 0 and header.e_shentsize != 0)
    {
        nHeaders = FromBytes<Elf64_Shdr>(data + header.e_shoff).sh_size;
    }
    sectionHeaders.resize(header.e_shnum);
    std::copy(data + header.e_shoff, data + header.e_shoff + sizeof(Elf64_Shdr) * header.e_shnum, reinterpret_cast<std::byte*>(sectionHeaders.data()));
}

std::string_view ldb::Elf::GetSectionName(std::size_t index) const
{
    // get string table section header: .shstrtab
    auto& section = sectionHeaders[header.e_shstrndx];
    return { reinterpret_cast<char*>(data) + section.sh_offset + index };
}

void ldb::Elf::BuildSectionMap()
{
    for (auto& section : sectionHeaders)
    {
        sectionMap[GetSectionName(section.sh_name)] = &section;
    }
}


std::optional<const Elf64_Shdr*> ldb::Elf::GetSection(std::string_view name) const
{
    if (sectionMap.contains(name))
    {
        // operator[] my add default value to unordered_map, so the object must be no-const
        // return sectionMap[name];
        return sectionMap.at(name);
    }
    return std::nullopt;
}

ldb::Span<const std::byte> ldb::Elf::GetSectionContents(std::string_view name) const
{
    if (auto section = GetSection(name); section)
    {
        return { data + section.value()->sh_offset, data + section.value()->sh_size };
    }
    return { nullptr, std::size_t{ 0 } };
}
