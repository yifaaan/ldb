#pragma once

#include <libldb/detail/dwarf.h>

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <libldb/types.hpp>
#include <memory>
#include <optional>
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
  std::uint64_t has_children;
  std::vector<AttrSpec> attrs;
};

class CompileUnit;
class Die;
class Attr {
 public:
  Attr(const CompileUnit* compile_unit, std::uint64_t type, std::uint64_t form,
       const std::byte* location)
      : compile_unit_{compile_unit},
        type_{type},
        form_{form},
        location_{location} {}

  std::uint64_t name() const { return type_; }

  std::uint64_t form() const { return form_; }

  // The attribute is DW_FORM_addr
  FileAddr AsAddress() const;

  // The attribute is DW_FORM_sec_offset
  std::uint32_t AsSectionOffset() const;

  // The attribute is DW_FORM_block
  std::span<const std::byte> AsBlock() const;

  std::uint64_t AsInt() const;

  std::string_view AsString() const;

  // The attribute is DW_FORM_ref_addr: a reference to a DIE in the same compile
  // unit.
  Die AsReference() const;

 private:
  const CompileUnit* compile_unit_;
  std::uint64_t type_;
  std::uint64_t form_;
  const std::byte* location_;
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

  const std::unordered_map<std::uint64_t, ldb::Abbrev>& abbrev_table() const;

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

// DIE 在 .debug_info 节中的结构非常紧凑，不包含 form 信息
// 因此需要通过 abbrev_table 来获取 form 信息
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
        abbrev_entry_{abbrev},
        attr_locations_{std::move(attr_locations)},
        next_{next} {}

  const CompileUnit* compile_unit() const { return compile_unit_; }

  const Abbrev* abbrev_entry() const { return abbrev_entry_; }

  const std::byte* next() const { return next_; }

  const std::byte* position() const { return position_; }

  // Check if the DIE contains the attribute.
  bool Contains(std::uint64_t attribute) const;

  // Get the attribute value.
  Attr operator[](std::uint64_t attribute) const;

  class ChildrenRange;
  ChildrenRange children() const;

 private:
  const std::byte* position_ = nullptr;
  const CompileUnit* compile_unit_ = nullptr;
  const Abbrev* abbrev_entry_ = nullptr;
  const std::byte* next_ = nullptr;
  std::vector<const std::byte*> attr_locations_;
};

class Die::ChildrenRange {
 public:
  explicit ChildrenRange(Die die) : die_{std::move(die)} {}

  class iterator {
   public:
    using value_type = Die;
    using reference = const Die&;
    using pointer = const Die*;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::forward_iterator_tag;

    iterator() = default;
    iterator(const iterator&) = default;
    iterator& operator=(const iterator&) = default;

    explicit iterator(const Die& die);

    const Die& operator*() const { return *die_; }
    const Die* operator->() const { return &die_.value(); }

    iterator& operator++();
    iterator operator++(int);

    bool operator==(const iterator& rhs) const;
    bool operator!=(const iterator& rhs) const { return !(*this == rhs); }

   private:
    std::optional<Die> die_;
  };

  iterator begin() const {
    if (die_.abbrev_entry_->has_children) {
      return iterator{die_};
    }
    return end();
  }

  iterator end() const { return {}; }

 private:
  Die die_;
};
}  // namespace ldb