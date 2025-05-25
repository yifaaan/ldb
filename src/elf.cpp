#include <cxxabi.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <libldb/bit.hpp>
#include <libldb/elf.hpp>
#include <libldb/error.hpp>

namespace ldb {
Elf::Elf(const std::filesystem::path& _path) : path_(_path) {
  if ((fd_ = open(path_.c_str(), O_LARGEFILE, O_RDONLY)) < 0) {
    Error::SendErrno("Could not open ELF file");
  }
  struct stat st;
  if (fstat(fd_, &st) < 0) {
    Error::SendErrno("Could not retrieve file stats");
  }
  file_size_ = st.st_size;
  void* ret;
  if ((ret = mmap(0, file_size_, PROT_READ, MAP_SHARED, fd_, 0)) == MAP_FAILED) {
    close(fd_);
    Error::SendErrno("Could not mmap ELF file");
  }
  data_ = reinterpret_cast<std::byte*>(ret);
  std::copy(data_, data_ + sizeof(header_), AsBytes(header_));

  ParseSectionHeaders();
  BuildSectionMap();
  ParseSymbolTable();
  BuildSymbolMaps();
  dwarf_ = std::make_unique<Dwarf>(*this);
}

Elf::~Elf() {
  munmap(data_, file_size_);
  close(fd_);
}

std::string_view Elf::GetSectionName(std::size_t index) const {
  // the section that stores the string table for section names(usually
  // .shstrtab)
  auto& section = section_headers_[header_.e_shstrndx];
  return {reinterpret_cast<char*>(data_) + section.sh_offset + index};
}

std::optional<const Elf64_Shdr*> Elf::GetSection(std::string_view name) const {
  if (section_map_.contains(name)) {
    return section_map_.at(name);
  }
  return std::nullopt;
}

Span<const std::byte> Elf::GetSectionContents(std::string_view name) const {
  if (auto section = GetSection(name); section) {
    return {data_ + section.value()->sh_offset, section.value()->sh_size};
  }
  return {nullptr, std::size_t(0)};
}

std::string_view Elf::GetString(std::size_t index) const {
  auto strtab = GetSection(".strtab");
  if (!strtab) {
    strtab = GetSection(".dynstr");
    if (!strtab) return {};
  }
  return {reinterpret_cast<char*>(data_) + strtab.value()->sh_offset + index};
}

const Elf64_Shdr* Elf::GetSectionContainingAddress(FileAddr addr) const {
  if (addr.ElfFile() != this) return nullptr;
  for (auto& sh : section_headers_) {
    if (sh.sh_addr <= addr.Addr() && addr.Addr() < sh.sh_addr + sh.sh_size) {
      return &sh;
    }
  }
  return nullptr;
}

const Elf64_Shdr* Elf::GetSectionContainingAddress(VirtAddr addr) const {
  for (auto& sh : section_headers_) {
    if (LoadBias() + sh.sh_addr <= addr && addr < LoadBias() + sh.sh_addr + sh.sh_size) {
      return &sh;
    }
  }
  return nullptr;
}

std::optional<FileAddr> Elf::GetSectonStartAddress(std::string_view name) const {
  if (auto sh = GetSection(name); sh) {
    return FileAddr{*this, sh.value()->sh_addr};
  }
  return std::nullopt;
}

std::vector<const Elf64_Sym*> Elf::GetSymbolByName(std::string_view name) const {
  auto [begin, end] = symbol_name_map_.equal_range(name);
  std::vector<const Elf64_Sym*> ret;
  std::transform(begin, end, std::back_inserter(ret), [](const auto& p) { return p.second; });
  return ret;
}

std::optional<const Elf64_Sym*> Elf::GetSymbolAtAddress(FileAddr addr) const {
  if (addr.ElfFile() != this) return std::nullopt;
  if (auto it = symbol_addr_map_.find({addr, {}}); it != symbol_addr_map_.end()) {
    return it->second;
  }
  return std::nullopt;
}

std::optional<const Elf64_Sym*> Elf::GetSymbolAtAddress(VirtAddr addr) const {
  return GetSymbolAtAddress(addr.ToFileAddr(*this));
}

std::optional<const Elf64_Sym*> Elf::GetSymbolContainingAddress(FileAddr addr) const {
  if (addr.ElfFile() != this || symbol_addr_map_.empty()) return std::nullopt;

  auto it = symbol_addr_map_.lower_bound({addr, {}});
  if (it != symbol_addr_map_.end()) {
    if (auto [k, v] = *it; k.first == addr) {
      return v;
    }
  }
  if (it == symbol_addr_map_.begin()) return std::nullopt;
  --it;
  if (auto [k, v] = *it; k.first < addr && addr < k.second) {
    return v;
  }
  return std::nullopt;
}

std::optional<const Elf64_Sym*> Elf::GetSymbolContainingAddress(VirtAddr addr) const {
  return GetSymbolContainingAddress(addr.ToFileAddr(*this));
}

void Elf::ParseSectionHeaders() {
  auto nHeaders = header_.e_shnum;
  if (nHeaders == 0 && header_.e_shentsize != 0) {
    nHeaders = FromBytes<Elf64_Shdr>(data_ + header_.e_shoff).sh_size;
  }
  section_headers_.resize(nHeaders);
  std::copy(data_ + header_.e_shoff, data_ + header_.e_shoff + sizeof(Elf64_Shdr) * nHeaders,
            reinterpret_cast<std::byte*>(section_headers_.data()));
}

void Elf::BuildSectionMap() {
  for (auto& section : section_headers_) {
    section_map_[GetSectionName(section.sh_name)] = &section;
  }
}

void Elf::ParseSymbolTable() {
  auto optTab = GetSection(".symtab");
  if (!optTab) {
    optTab = GetSection(".dynsym");
    if (!optTab) return;
  }
  auto symtab = *optTab;
  symbol_table_.resize(symtab->sh_size / symtab->sh_entsize);
  std::copy(data_ + symtab->sh_offset, data_ + symtab->sh_offset + symtab->sh_size,
            reinterpret_cast<std::byte*>(symbol_table_.data()));
}

void Elf::BuildSymbolMaps() {
  for (auto& sym : symbol_table_) {
    auto mangledName = GetString(sym.st_name);
    int status;
    auto demangledName = abi::__cxa_demangle(mangledName.data(), nullptr, nullptr, &status);
    if (status == 0) {
      symbol_name_map_.emplace(demangledName, &sym);
      free(demangledName);
    }
    symbol_name_map_.emplace(mangledName, &sym);

    if (sym.st_value != 0 && sym.st_name != 0 && ELF64_ST_TYPE(sym.st_info) != STT_TLS) {
      auto addrRange = std::make_pair(FileAddr{*this, sym.st_value}, FileAddr{*this, sym.st_value + sym.st_size});
      symbol_addr_map_.emplace(addrRange, &sym);
    }
  }
}
}  // namespace ldb