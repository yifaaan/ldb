#pragma once

#include <elf.h>

#include <filesystem>
#include <libldb/dwarf.hpp>
#include <libldb/types.hpp>
#include <map>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ldb {
class Elf {
 public:
  Elf(const std::filesystem::path& path);
  ~Elf();

  Elf(const Elf&) = delete;
  Elf& operator=(const Elf&) = delete;
  Elf(Elf&&) = delete;
  Elf& operator=(Elf&&) = delete;

  std::filesystem::path Path() const { return path_; }

  VirtAddr LoadBias() const { return load_bias_; }
  void NotifyLoaded(VirtAddr addr) { load_bias_ = addr; }

  const Elf64_Ehdr& GetHeader() const { return header_; }

  std::string_view GetSectionName(std::size_t index) const;

  std::optional<const Elf64_Shdr*> GetSection(std::string_view name) const;

  Span<const std::byte> GetSectionContents(std::string_view name) const;

  std::string_view GetString(std::size_t index) const;

  const Elf64_Shdr* GetSectionContainingAddress(FileAddr addr) const;
  const Elf64_Shdr* GetSectionContainingAddress(VirtAddr addr) const;

  std::optional<FileAddr> GetSectonStartAddress(std::string_view name) const;

  std::vector<const Elf64_Sym*> GetSymbolByName(std::string_view name) const;

  std::optional<const Elf64_Sym*> GetSymbolAtAddress(FileAddr addr) const;
  std::optional<const Elf64_Sym*> GetSymbolAtAddress(VirtAddr addr) const;

  std::optional<const Elf64_Sym*> GetSymbolContainingAddress(FileAddr addr) const;
  std::optional<const Elf64_Sym*> GetSymbolContainingAddress(VirtAddr addr) const;

  Dwarf& GetDwarf() { return *dwarf_; }
  const Dwarf& GetDwarf() const { return *dwarf_; }

  FileOffset DataPointerAsFileOffset(const std::byte* ptr) const {
    return FileOffset{*this, static_cast<std::uint64_t>(ptr - data_)};
  }

  const std::byte* FileOffsetAsDataPointer(FileOffset offset) const { return data_ + offset.Offset(); }

 private:
  void ParseSectionHeaders();

  void BuildSectionMap();

  void ParseSymbolTable();

  void BuildSymbolMaps();

  static constexpr auto RangeComparator = [](std::pair<FileAddr, FileAddr> a, std::pair<FileAddr, FileAddr> b) {
    return a.first < b.first;
  };

  int fd_;
  std::filesystem::path path_;
  std::size_t file_size_;
  std::byte* data_;
  Elf64_Ehdr header_;
  VirtAddr load_bias_;

  std::vector<Elf64_Shdr> section_headers_;

  std::unordered_map<std::string_view, Elf64_Shdr*> section_map_;

  std::vector<Elf64_Sym> symbol_table_;

  std::unordered_multimap<std::string_view, Elf64_Sym*> symbol_name_map_;
  std::map<std::pair<FileAddr, FileAddr>, Elf64_Sym*, decltype(RangeComparator)> symbol_addr_map_;

  std::unique_ptr<Dwarf> dwarf_;
};
}  // namespace ldb