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
class RangeList;
// A die has a list of attributes.
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

  // The attribute is DW_AT_ranges. Then the value is a section offset.
  RangeList AsRangeList() const;

 private:
  const CompileUnit* compile_unit_;
  std::uint64_t type_;
  std::uint64_t form_;
  const std::byte* location_;
};

class CompileUnit;
// base address selector
// +--------------------+--------------------+
// | 全部位设为 1       | 新基址            |
// +--------------------+--------------------+
//   0xFFFFFFFFFFFFFFFF    （实际基地址值）

// range list
// +--------------------+--------------------+
// | 起始偏移量         | 结束偏移量         |
// +--------------------+--------------------+

// end of list
// +--------------------+--------------------+
// | 0                  | 0                  |
// +--------------------+--------------------+

// 0xFFFFFFFFFFFFFFFF 0x0000000000400000  // 基址选择器，设置基址为 0x400000
// 0x0000000000000010 0x0000000000000020  // 范围 1：0x400010-0x400020
// 0x0000000000000030 0x0000000000000040  // 范围 2：0x400030-0x400040
// 0xFFFFFFFFFFFFFFFF 0x0000000000500000  // 新基址选择器，更改基址为 0x500000
// 0x0000000000000005 0x0000000000000015  // 范围 3：0x500005-0x500015
// 0x0000000000000000 0x0000000000000000  // 结束指示器

// In section .debug_ranges, to
class RangeList {
 public:
  RangeList(const CompileUnit* compile_unit, std::span<const std::byte> data,
            FileAddr base_addr)
      : compile_unit_{compile_unit}, data_{data}, base_addr_{base_addr} {}

  struct Entry {
    FileAddr low;
    FileAddr high;

    bool Contains(FileAddr addr) const { return low <= addr && addr < high; }
  };

  class iterator;
  iterator begin() const;
  iterator end() const;
  bool Contains(FileAddr addr) const;

 private:
  const CompileUnit* compile_unit_;
  std::span<const std::byte> data_;
  FileAddr base_addr_;
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
  Die root() const;

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

  const CompileUnit* CompileUnitContainingAddress(FileAddr address) const;

  std::optional<Die> FunctionContainingAddress(FileAddr address) const;

  std::vector<Die> FindFunctions(std::string name) const;

 private:
  // Indexing the entire set of DIEs
  void Index() const;

  // Indexing a single DIE
  void IndexDie(const Die& current) const;

  struct IndexEntry {
    const CompileUnit* compile_unit;
    const std::byte* position;
  };

  const Elf* elf_;

  // One compile unit has one abbrev table.
  // One executable can have multiple abbrev tables, indexed by offset.
  std::unordered_map<std::size_t, std::unordered_map<std::uint64_t, Abbrev>>
      abbrev_tables_;

  std::vector<std::unique_ptr<CompileUnit>> compile_units_;

  mutable std::unordered_multimap<std::string, IndexEntry> function_index_;
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

  FileAddr LowPc() const;
  FileAddr HighPc() const;

  bool ContainsAddress(FileAddr addr) const;

  std::optional<std::string_view> Name() const;

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

    reference operator*() const { return *die_; }
    pointer operator->() const { return &die_.value(); }

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

class RangeList::iterator {
 public:
  using value_type = RangeList::Entry;
  using reference = const RangeList::Entry&;
  using pointer = const RangeList::Entry*;
  using difference_type = std::ptrdiff_t;
  using iterator_category = std::forward_iterator_tag;

  iterator() = default;
  iterator(const iterator&) = default;
  iterator& operator=(const iterator&) = default;

  iterator(const CompileUnit* cu, std::span<const std::byte> data,
           FileAddr base_addr);

  reference operator*() const { return current_; }

  pointer operator->() const { return &current_; }

  bool operator==(const iterator& rhs) const {
    return position_ == rhs.position_;
  }
  bool operator!=(const iterator& rhs) const { return !(*this == rhs); }

  iterator& operator++();
  iterator operator++(int);

 private:
  const CompileUnit* compile_unit_ = nullptr;
  std::span<const std::byte> data_;
  FileAddr base_addr_;
  const std::byte* position_ = nullptr;
  Entry current_;
};
}  // namespace ldb