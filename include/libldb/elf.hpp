#pragma once

#include <elf.h>
#include <spdlog/spdlog.h>

#include <filesystem>
#include <libldb/types.hpp>
#include <map>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace ldb {
class Elf {
 public:
  explicit Elf(const std::filesystem::path& path);
  ~Elf();

  Elf(const Elf&) = delete;
  Elf& operator=(const Elf&) = delete;

  std::filesystem::path path() const { return path_; }

  const Elf64_Ehdr& header() const { return header_; }

  // Get the name of a section.
  // Section name string table, which lives in the
  // .shstrtab section, is indexed by the sh_name field of each section header.
  std::string_view GetSectionName(std::size_t index) const;

  // Get the section header for a given section name.
  std::optional<const Elf64_Shdr*> GetSectionHeader(
      std::string_view name) const;

  // Get the contents of a section.
  std::span<const std::byte> GetSectionContents(std::string_view name) const;

  // Get the general string from .strtab or .dynstr sections.
  std::string_view GetString(std::size_t index) const;

  // The difference between the load address and the file address.
  VirtAddr load_bias() const { return load_bias_; }

  // Notify the ELF that it has been loaded at a given address.
  void NotifyLoaded(VirtAddr address) { load_bias_ = address; }

  // Get the section header containing a given file address.
  const Elf64_Shdr* GetSectionHeaderContainingAddress(FileAddr addr) const;

  // Get the section header containing a given virtualaddress.
  const Elf64_Shdr* GetSectionHeaderContainingAddress(VirtAddr addr) const;

  // Get the start file address of a section.
  std::optional<FileAddr> GetSectionStartFileAddress(
      std::string_view name) const;

  // Get all symbols with a given name.
  std::vector<const Elf64_Sym*> GetSymbolsByName(std::string_view name) const;

  // Get the symbol at a given file address.
  std::optional<const Elf64_Sym*> GetSymbolAtAddress(FileAddr addr) const;

  // Get the symbol at a given virtual address.
  std::optional<const Elf64_Sym*> GetSymbolAtAddress(VirtAddr addr) const;

  // Get the symbol containing a given file address.
  std::optional<const Elf64_Sym*> GetSymbolContainingAddress(
      FileAddr addr) const;

  // Get the symbol containing a given virtual address.
  std::optional<const Elf64_Sym*> GetSymbolContainingAddress(
      VirtAddr addr) const;

  // Debug function to print the symbol map.
  void PrintInfo() const {
    SPDLOG_INFO("path: {}", path_.string());
    SPDLOG_INFO("load_bias: {}", load_bias_.addr());
    SPDLOG_INFO("section_headers_.size(): {}", section_headers_.size());
    SPDLOG_INFO("symbol_addr_map_.size(): {}", symbol_addr_map_.size());
  }

 private:
  // Parse the section headers from the ELF file.
  void ParseSectionHeaders();

  // Build a map of section names to section headers.
  void BuildSectionNameMap();

  // Parse the symbol table from the ELF file.
  void ParseSymbolTable();

  // Build the symbol map from the ELF file.
  void BuildSymbolMap();

 private:
  std::filesystem::path path_;
  int fd_;
  std::size_t file_size_;
  std::byte* data_;

  // Elf header
  Elf64_Ehdr header_;

  // Section headers
  std::vector<Elf64_Shdr> section_headers_;

  // Map of section names to section headers
  std::unordered_map<std::string_view, Elf64_Shdr*> section_header_map_;

  // The difference between the load address and the file address.
  VirtAddr load_bias_;

  // Symbol table
  std::vector<Elf64_Sym> symbol_table_;

  // Map of symbol names to symbol table entries
  std::unordered_multimap<std::string_view, Elf64_Sym*> symbol_name_map_;

  static constexpr auto RangeComparator = [](const auto& a, const auto& b) {
    return a.first < b.first;
  };
  // Map of symbol addresses to symbol table entries
  std::map<std::pair<FileAddr, FileAddr>, Elf64_Sym*, decltype(RangeComparator)>
      symbol_addr_map_;
};
}  // namespace ldb