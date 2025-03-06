#include <elf.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <libldb/elf.hpp>
#include <libldb/error.hpp>

#include "libldb/bit.hpp"

ldb::Elf::Elf(const std::filesystem::path& path) : path_{path} {
  if ((fd_ = open(path.c_str(), O_LARGEFILE | O_RDONLY)) < 0) {
    Error::SendErrno("Could not open ELF file");
  }

  struct stat stats;
  if (fstat(fd_, &stats) < 0) {
    Error::SendErrno("Could not retrieve ELF file stats");
  }
  file_size_ = stats.st_size;

  void* ret;
  if ((ret = mmap(nullptr, file_size_, PROT_READ, MAP_SHARED, fd_, 0))) {
    close(fd_);
    Error::SendErrno("Could not mmap ELF file");
  }
  data_ = reinterpret_cast<std::byte*>(ret);

  // Copy the ELF header into the header_ member
  std::copy(data_, data_ + sizeof(header_), AsBytes(header_));

  ParseSectionHeaders();
  BuildSectionNameMap();
}

ldb::Elf::~Elf() {
  munmap(data_, file_size_);
  close(fd_);
}

std::string_view ldb::Elf::GetSectionName(std::size_t index) const {
  // The Section name string table, which lives in the .shstrtab section
  const auto& section_name_string_table_header =
      section_headers_[header_.e_shstrndx];
  return {reinterpret_cast<char*>(data_) +
          section_name_string_table_header.sh_offset + index};
}

std::optional<const Elf64_Shdr*> ldb::Elf::GetSectionHeader(
    std::string_view name) const {
  if (section_header_map_.contains(name)) {
    return section_header_map_.at(name);
  }
  return std::nullopt;
}

std::span<const std::byte> ldb::Elf::GetSectionContents(
    std::string_view name) const {
  if (auto header = GetSectionHeader(name); header) {
    return {
        reinterpret_cast<const std::byte*>(data_ + header.value()->sh_offset),
        header.value()->sh_size};
  }
  return {};
}

std::string_view ldb::Elf::GetString(std::size_t index) const {
  auto opt_strtab_header = GetSectionHeader(".strtab");
  if (!opt_strtab_header) {
    opt_strtab_header = GetSectionHeader(".dynstr");
    if (!opt_strtab_header) return "";
  }

  return {reinterpret_cast<char*>(data_) +
          opt_strtab_header.value()->sh_offset + index};
}

const Elf64_Shdr* ldb::Elf::GetSectionHeaderContainingAddress(
    FileAddr addr) const {
  if (addr.elf() != this) return nullptr;

  if (auto it = std::ranges::find_if(section_headers_,
                                     [&addr](const auto& header) {
                                       return header.sh_addr <= addr.addr() &&
                                              addr.addr() < header.sh_addr +
                                                                header.sh_size;
                                     });
      it != std::end(section_headers_)) {
    return &*it;
  }
  return nullptr;
}

const Elf64_Shdr* ldb::Elf::GetSectionHeaderContainingAddress(
    VirtAddr addr) const {
  if (auto it = std::ranges::find_if(
          section_headers_,
          [&addr, this](const auto& header) {
            return load_bias() + header.sh_addr <= addr &&
                   addr < load_bias() + header.sh_addr + header.sh_size;
          });
      it != std::end(section_headers_)) {
    return &*it;
  }
  return nullptr;
}

std::optional<ldb::FileAddr> ldb::Elf::GetSectionStartFileAddress(
    std::string_view name) const {
  if (auto section_header = GetSectionHeader(name); section_header) {
    return FileAddr{*this, section_header.value()->sh_addr};
  }
  return std::nullopt;
}

void ldb::Elf::ParseSectionHeaders() {
  // if a file has 0xff00 sections or more, it sets
  // e_shnum to 0 and stores the number of sections in the sh_size field of the
  // first section header.
  auto n_header = header_.e_shnum;
  if (n_header == 0 && header_.e_shentsize != 0) {
    n_header = FromBytes<Elf64_Shdr>(data_ + header_.e_shoff).sh_size;
  }
  section_headers_.resize(n_header);
  std::copy(data_ + header_.e_shoff,
            data_ + header_.e_shoff + n_header * sizeof(Elf64_Shdr),
            reinterpret_cast<std::byte*>(section_headers_.data()));
}

void ldb::Elf::BuildSectionNameMap() {
  std::ranges::for_each(section_headers_, [this](auto& header) {
    auto name = GetSectionName(header.sh_name);
    section_header_map_[name] = &header;
  });
}