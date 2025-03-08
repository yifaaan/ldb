#pragma once

#include <libldb/detail/dwarf.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
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

class Die;
class Dwarf;
// .debug_info section is a list of compile units.
// Each compile unit has a header and a list of DIEs.
// Header:
//  unit_length (4或12字节)：
//    - 如果初始4字节不是0xffffffff，那么长度为4字节
//    - 如果是0xffffffff，那么接下来的8字节表示长度
//  version (2字节)：DWARF版本号
//  abbrev_offset (4或8字节)：
//    - 指向.debug_abbrev中的缩写表偏移量
//  address_size (1字节)：
//    - 目标机器的地址大小（通常是4或8）

// DIE:
//   abbrev_code (ULEB128)：对应缩写表中的代码
//   attribute_values：根据缩写表中指定的属性列表提供的值
// 编译单元DIE
//   ├── 子DIE 1
//   │     ├── 孙DIE 1
//   │     └── 孙DIE 2
//   ├── 子DIE 2
//   └── 子DIE 3
//         └── 孙DIE 3
class CompileUnit {
 public:
  // data is the content of .debug_info section.
  // abbrev_offset is the offset of .debug_abbrev section.
  CompileUnit(Dwarf& dwarf, std::span<const std::byte> data,
              std::size_t abbrev_offset)
      : dwarf_{&dwarf}, data_{data}, abbrev_offset_{abbrev_offset} {}

  const Dwarf* dwarf() const { return dwarf_; }

  std::span<const std::byte> data() const { return data_; }

  const std::unordered_map<std::uint64_t, ldb::Abbrev>& abbrev_table();

  // The compile unit's root DIE.
  Die root();

 private:
  Dwarf* dwarf_ = nullptr;
  std::span<const std::byte> data_;
  std::size_t abbrev_offset_;
};

class Elf;
class Dwarf {
 public:
  explicit Dwarf(const Elf& elf);
  const Elf* elf() const { return elf_; }

  // Get abbrev table by offset of .debug_abbrev section.
  const std::unordered_map<std::uint64_t, Abbrev>& GetAbbrevTable(
      std::size_t offset);

  // Get compile units.
  const std::vector<std::unique_ptr<CompileUnit>>& compile_units() const {
    return compile_units_;
  }

 private:
  const Elf* elf_;

  // One compile unit has one abbrev table.
  // One executable can have multiple abbrev tables, indexed by offset.
  std::unordered_map<std::size_t, std::unordered_map<std::uint64_t, Abbrev>>
      abbrev_tables_;

  std::vector<std::unique_ptr<CompileUnit>> compile_units_;
};

// DIE A (函数)
// +------------------------+
// | Abbrev Code (ULEB128) |  // 1: DW_TAG_subprogram
// +------------------------+
// | Attribute Values:      |
// |   DW_AT_name: "main"   |
// |   DW_AT_low_pc: 0x1000  |
// |   DW_AT_high_pc: 0x1100  |
// +------------------------+
// | Children: yes          |  // has_children = 1
// +------------------------+

//   // 子项 DIE B (参数)
//   DIE B
//   +------------------------+
//   | Abbrev Code (ULEB128) |  // 2: DW_TAG_formal_parameter
//   +------------------------+
//   | Attribute Values:      |
//   |   DW_AT_name: "arg1"   |
//   |   DW_AT_type: DW_TYPE_int |
//   +------------------------+
//   | Children: no           |  // has_children = 0
//   +------------------------+

//   // NULL 终止
//   DIE NULL
//   +------------------------+
//   | Abbrev Code: 0        |  // 终止标记
//   +------------------------+
class Die {
 public:
  explicit Die(const std::byte* next) : next_{next} {}

  Die(const std::byte* position, const CompileUnit* compile_unit,
      const Abbrev* abbrev, std::vector<const std::byte*> attr_locations,
      const std::byte* next)
      : position_{position},
        compile_unit_{compile_unit},
        abbrev_{abbrev},
        attr_locations_{std::move(attr_locations)},
        next_{next} {}

  const CompileUnit* compile_unit() const { return compile_unit_; }

  const Abbrev* abbrev() const { return abbrev_; }

  const std::byte* next() const { return next_; }

  const std::byte* position() const { return position_; }

 private:
  const std::byte* position_ = nullptr;
  const CompileUnit* compile_unit_ = nullptr;
  const Abbrev* abbrev_ = nullptr;
  const std::byte* next_ = nullptr;
  std::vector<const std::byte*> attr_locations_;
};
}  // namespace ldb