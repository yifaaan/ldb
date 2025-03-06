#pragma once

#include <elf.h>

#include <filesystem>
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

 private:
  // Parse the section headers from the ELF file.
  void ParseSectionHeaders();

  // Build a map of section names to section headers.
  void BuildSectionNameMap();

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
};
}  // namespace ldb