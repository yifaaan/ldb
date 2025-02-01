
#include "libldb/types.hpp"
#include <algorithm>
#include <elf.h>
#include <fmt/base.h>
#include <optional>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <cxxabi.h>



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

    ParseSectionHeaders();
    BuildSectionMap();
    ParseSymbolTable();
    BuildSymbolMaps();
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

std::string_view ldb::Elf::GetString(std::size_t index) const
{
    if (auto strtab = GetSection(".strtab"); strtab)
    {
        return { reinterpret_cast<char*>(data) + strtab.value()->sh_offset + index };
    }
    else
    {
        strtab = GetSection(".dynstr");
        if (!strtab) return "";
    }
}

const Elf64_Shdr* ldb::Elf::GetSectionContainingAddress(FileAddr addr) const
{
    if (addr.ElfFile() != this)
    {
        return nullptr;
    }
    for (const auto& sh : sectionHeaders)
    {
        auto begin = sh.sh_addr;
        auto end = sh.sh_addr + sh.sh_size;
        if (begin <= addr.Addr() && addr.Addr() < end)
        {
            return &sh;
        }
    }
    return nullptr;
}


const Elf64_Shdr* ldb::Elf::GetSectionContainingAddress(VirtAddr addr) const
{
    for (const auto& sh : sectionHeaders)
    {
        auto begin = loadBias + sh.sh_addr;
        auto end = loadBias + sh.sh_addr + sh.sh_size;
        if (begin <= addr and addr < end)
        {
            return &sh;
        }
    }
    return nullptr;
}

void ldb::Elf::ParseSymbolTable()
{
    auto symtab = GetSection(".symtab");

    if (!symtab)
    {
        symtab = GetSection(".dynsym");
        if (!symtab) return;
    }

    
    auto tableHeader = *symtab;
    symbolTable.resize(tableHeader->sh_size / tableHeader->sh_entsize);
    std::copy(data + tableHeader->sh_offset, data + tableHeader->sh_offset + tableHeader->sh_size, reinterpret_cast<std::byte*>(symbolTable.data()));
}

void ldb::Elf::BuildSymbolMaps()
{
    for (auto& symbol : symbolTable)
    {
        auto mangledName = GetString(symbol.st_name);
        int demangleStatus = -1;
        auto demangledName = abi::__cxa_demangle(mangledName.data(), nullptr, nullptr, &demangleStatus);
        if (demangleStatus == 0)
        {
            symbolNameMap.insert({ demangledName, &symbol });
            free(demangledName);
        }
        symbolNameMap.insert({ mangledName, &symbol });

        if (symbol.st_value != 0 and symbol.st_name != 0 and ELF64_ST_TYPE(symbol.st_info) != STT_TLS)
        {
            auto addrRange = std::make_pair<FileAddr, FileAddr>({ *this, symbol.st_value }, { *this, symbol.st_value + symbol.st_size });
            symbolAddrMap.insert({ addrRange, &symbol });
        }
    }
}

std::vector<const Elf64_Sym*> ldb::Elf::GetSymbolsByName(std::string_view name) const
{
    auto [begin, end] = symbolNameMap.equal_range(name);
    std::vector<const Elf64_Sym*> ret;
    std::transform(begin, end, std::back_inserter(ret), [](const auto& p)
    {
        return p.second;
    });
    return ret;
}

std::optional<const Elf64_Sym*> ldb::Elf::GetSymbolAtAddress(FileAddr address) const
{
    if (address.ElfFile() != this)
    {
        return std::nullopt;
    }
    if (auto it = symbolAddrMap.find({ address, {} }); it != std::end(symbolAddrMap))
    {
        return it->second;
    }
    return std::nullopt;
}

std::optional<const Elf64_Sym*> ldb::Elf::GetSymbolAtAddress(VirtAddr address) const
{
    return GetSymbolAtAddress(address.ToFileAddr(*this));
}

std::optional<const Elf64_Sym*> ldb::Elf::GetSymbolContainingAddress(FileAddr address) const
{
    if (address.ElfFile() != this or symbolAddrMap.empty())
    {
        return std::nullopt;
    }
    if (auto it = symbolAddrMap.lower_bound({ address, {} }); it != std::end(symbolAddrMap))
    {
        auto [first, second] = *it;
        if (first.first == address)
        {
            return second;
        }
        if (it == std::begin(symbolAddrMap))
        {
            return std::nullopt;
        }
        --it;
        if (auto [a, b] = *it; a.first < address and address < a.second)
        {
            return b;
        }
    }
    return std::nullopt;
}

std::optional<const Elf64_Sym*> ldb::Elf::GetSymbolContainingAddress(VirtAddr address) const
{
    return GetSymbolContainingAddress(address.ToFileAddr(*this));
}