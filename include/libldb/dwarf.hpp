#pragma once

#include <libldb/detail/dwarf.h>

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>
namespace ldb {

struct AttrSpec {
  std::uint64_t attr;
  std::uint64_t form;
};

// Abbreviation Entry:
//   +------------------------+
//   | Abbrev Code (ULEB128) |
//   +------------------------+
//   | TAG (ULEB128)         |
//   +------------------------+
//   | Children Flag (1 byte) |
//   +------------------------+
//   | Attribute Specs:       |
//   |   name1 (ULEB128)     |
//   |   form1 (ULEB128)     |
//   |   name2 (ULEB128)     |
//   |   form2 (ULEB128)     |
//   |   ...                 |
//   |   0, 0 (终止对)       |
//   +------------------------+
struct Abbrev {
  std::uint64_t code;
  std::uint64_t tag;
  std::uint64_t children;
  std::vector<AttrSpec> attrs;
};

class Elf;
class Dwarf {
 public:
  explicit Dwarf(const Elf& elf);
  const Elf* elf() const { return elf_; }

  // Get abbrev table by offset of .debug_abbrev section.
  const std::unordered_map<std::uint64_t, Abbrev>& GetAbbrevTable(
      std::size_t offset);

 private:
  const Elf* elf_;

  // One compile unit has one abbrev table.
  // One executable can have multiple abbrev tables, indexed by offset.
  std::unordered_map<std::size_t, std::unordered_map<std::uint64_t, Abbrev>>
      abbrev_tables_;
};
}  // namespace ldb