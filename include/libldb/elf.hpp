#pragma once

#include <elf.h>

#include <filesystem>
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

 private:
  void ParseSectionHeaders();

 private:
  std::filesystem::path path_;
  int fd_;
  std::size_t file_size_;
  std::byte* data_;

  // Elf header
  Elf64_Ehdr header_;

  // Section headers
  std::vector<Elf64_Shdr> section_headers_;
};
}  // namespace ldb