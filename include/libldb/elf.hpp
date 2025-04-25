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

  std::filesystem::path Path() const { return path; }

  VirtAddr LoadBias() const { return loadBias; }
  void NotifyLoaded(VirtAddr addr) { loadBias = addr; }

  const Elf64_Ehdr& GetHeader() const { return header; }

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

  std::optional<const Elf64_Sym*> GetSymbolContainingAddress(
      FileAddr addr) const;
  std::optional<const Elf64_Sym*> GetSymbolContainingAddress(
      VirtAddr addr) const;

  Dwarf& GetDwarf() { return *dwarf; }
  const Dwarf& GetDwarf() const { return *dwarf; }

 private:
  void ParseSectionHeaders();

  void BuildSectionMap();

  void ParseSymbolTable();

  void BuildSymbolMaps();

  static constexpr auto RangeComparator = [](std::pair<FileAddr, FileAddr> a,
                                             std::pair<FileAddr, FileAddr> b) {
    return a.first < b.first;
  };

  int fd;
  std::filesystem::path path;
  std::size_t fileSize;
  std::byte* data;
  Elf64_Ehdr header;
  VirtAddr loadBias;

  std::vector<Elf64_Shdr> sectionHeaders;

  std::unordered_map<std::string_view, Elf64_Shdr*> sectionMap;

  std::vector<Elf64_Sym> symbolTable;

  std::unordered_multimap<std::string_view, Elf64_Sym*> symbolNameMap;
  std::map<std::pair<FileAddr, FileAddr>, Elf64_Sym*, decltype(RangeComparator)>
      symbolAddrMap;

  std::unique_ptr<Dwarf> dwarf;
};
}  // namespace ldb