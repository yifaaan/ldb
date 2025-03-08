#include "libldb/detail/dwarf.h"

#include <algorithm>
#include <libldb/bit.hpp>
#include <libldb/dwarf.hpp>
#include <libldb/elf.hpp>
#include <libldb/error.hpp>
#include <span>

namespace {
class Cursor {
 public:
  explicit Cursor(std::span<const std::byte> data)
      : data_{data}, position_{data.data()} {}

  Cursor& operator++() {
    ++position_;
    return *this;
  }

  Cursor& operator+=(std::size_t n) {
    position_ += n;
    return *this;
  }

  const std::byte* positon() const { return position_; }

  bool Finished() const { return position_ >= data_.data() + data_.size(); }

  template <typename T>
  T FixedInt() {
    auto t = ldb::FromBytes<T>(position_);
    position_ += sizeof(T);
    return t;
  }

  std::uint8_t u8() { return FixedInt<std::uint8_t>(); }

  std::uint16_t u16() { return FixedInt<std::uint16_t>(); }

  std::uint32_t u32() { return FixedInt<std::uint32_t>(); }

  std::uint64_t u64() { return FixedInt<std::uint64_t>(); }

  std::int8_t s8() { return FixedInt<std::int8_t>(); }

  std::int16_t s16() { return FixedInt<std::int16_t>(); }

  std::int32_t s32() { return FixedInt<std::int32_t>(); }

  std::int64_t s64() { return FixedInt<std::int64_t>(); }

  std::string_view string() {
    auto null_terminator =
        std::find(position_, data_.data() + data_.size(), std::byte{0});
    std::string_view ret{reinterpret_cast<const char*>(position_),
                         static_cast<std::size_t>(null_terminator - position_)};
    position_ = null_terminator + 1;
    return ret;
  }

  std::uint64_t uleb128() {
    std::uint64_t result = 0;
    int shift = 0;
    std::uint8_t byte = 0;
    do {
      byte = u8();
      auto mask = static_cast<std::uint64_t>(byte & 0x7f);
      result |= mask << shift;
      shift += 7;
    } while ((byte & 0x80) != 0);
    return result;
  }

  std::int64_t sleb128() {
    std::int64_t result = 0;
    int shift = 0;
    std::uint8_t byte = 0;
    do {
      byte = s8();
      auto mask = static_cast<std::int64_t>(byte & 0x7f);
      result |= mask << shift;
      shift += 7;
    } while ((byte & 0x80) != 0);

    if ((shift < sizeof(result) * 8) && (byte & 0x40) != 0) {
      result |= ~static_cast<std::int64_t>(0) << shift;
    }
    return result;
  }

  void SkipForm(std::uint64_t form) {
    switch (form) {
      case DW_FORM_flag_present:
        break;

      case DW_FORM_data1:
      case DW_FORM_ref1:
      case DW_FORM_flag:
        position_ += 1;
        break;
      case DW_FORM_data2:
      case DW_FORM_ref2:
        position_ += 2;
        break;
      case DW_FORM_data4:
      case DW_FORM_ref4:
      case DW_FORM_ref_addr:
      case DW_FORM_sec_offset:
      case DW_FORM_strp:
        position_ += 4;
        break;
      case DW_FORM_data8:
      case DW_FORM_addr:
        position_ += 8;
        break;
      case DW_FORM_sdata:
        sleb128();
        break;
      case DW_FORM_udata:
      case DW_FORM_ref_udata:
        uleb128();
        break;
      case DW_FORM_block1:
        position_ += u8();
        break;
      case DW_FORM_block2:
        position_ += u16();
        break;
      case DW_FORM_block4:
        position_ += u32();
        break;
      case DW_FORM_block:
      case DW_FORM_exprloc:
        position_ += uleb128();
        break;
      case DW_FORM_string:
        while (!Finished() && *position_ != std::byte{0}) {
          ++position_;
        }
        ++position_;
      case DW_FORM_indirect:
        SkipForm(uleb128());
        break;
      default:
        ldb::Error::Send("Unrecognized DWARF form");
    }
  }

 private:
  std::span<const std::byte> data_;
  const std::byte* position_;
};

std::unordered_map<std::uint64_t, ldb::Abbrev> ParseAbbrevTable(
    const ldb::Elf& elf, std::size_t offset) {
  Cursor cursor{elf.GetSectionContents(".debug_abbrev")};
  cursor += offset;
  /*
  [Abbrev Entry 1]
  [Abbrev Entry 2]
  [Abbrev Entry 3]
  ...
  [Abbrev Entry N]
  [Terminating Entry (code = 0)]

  Abbreviation Entry:
  +------------------------+
  | Abbrev Code (ULEB128) |
  +------------------------+
  | TAG (ULEB128)         |
  +------------------------+
  | Children Flag (1 byte) |
  +------------------------+
  | Attribute Specs:       |
  |   name1 (ULEB128)     |
  |   form1 (ULEB128)     |
  |   name2 (ULEB128)     |
  |   form2 (ULEB128)     |
  |   ...                 |
  |   0, 0 (终止对)       |
  +------------------------+
  */
  std::unordered_map<std::uint64_t, ldb::Abbrev> table;
  std::uint64_t code = 0;
  do {
    // Parse entry
    code = cursor.uleb128();
    auto tag = cursor.uleb128();
    auto has_children = static_cast<bool>(cursor.u8());
    std::vector<ldb::AttrSpec> attrs;
    std::uint64_t attr = 0;
    do {
      attr = cursor.uleb128();
      auto form = cursor.uleb128();
      if (attr != 0) {
        attrs.emplace_back(attr, form);
      }
    } while (attr != 0);

    if (code != 0) {
      table.emplace(code,
                    ldb::Abbrev{code, tag, has_children, std::move(attrs)});
    }

  } while (code != 0);

  return table;
}

// Parse compile unit header.
std::unique_ptr<ldb::CompileUnit> ParseCompileUnit(ldb::Dwarf& dwarf,
                                                   const ldb::Elf& elf,
                                                   Cursor cursor) {
  auto start = cursor.positon();
  auto unit_length = cursor.u32();
  auto version = cursor.u16();
  auto abbrev_offset = cursor.u32();
  auto address_size = cursor.u8();

  if (unit_length == 0xffffffff) {
    ldb::Error::Send("Only DWARF32 is supported");
  }
  if (version != 4) {
    ldb::Error::Send("Only DWARF4 is supported");
  }
  if (address_size != 8) {
    ldb::Error::Send("Only 64-bit address size is supported");
  }

  // Plus the size of unit_length.
  unit_length += sizeof(std::uint32_t);
  std::span<const std::byte> data{start, unit_length};
  return std::make_unique<ldb::CompileUnit>(
      dwarf, data, static_cast<std::size_t>(abbrev_offset));
}

// Parse compile units.
std::vector<std::unique_ptr<ldb::CompileUnit>> ParseCompileUnits(
    ldb::Dwarf& dwarf, const ldb::Elf& elf) {
  auto debug_info = elf.GetSectionContents(".debug_info");
  Cursor cursor{debug_info};

  std::vector<std::unique_ptr<ldb::CompileUnit>> compile_units;
  while (!cursor.Finished()) {
    auto unit = ParseCompileUnit(dwarf, elf, cursor);
    cursor += unit->data().size();
    compile_units.emplace_back(std::move(unit));
  }
  return compile_units;
}

ldb::Die ParseDie(ldb::CompileUnit& compile_unit, Cursor cursor) {
  auto die_start = cursor.positon();
  auto abbrev_code = cursor.uleb128();
  if (abbrev_code == 0) {
    auto next = cursor.positon();
    return ldb::Die{next};
  }
  const auto& abbrev_table = compile_unit.abbrev_table();
  // DIE 1:
  //   tag: DW_TAG_subprogram
  //   has_children: yes
  //   attributes:
  //     - name: DW_AT_name, form: DW_FORM_string
  //     - name: DW_AT_low_pc, form: DW_FORM_addr
  //     - name: DW_AT_high_pc, form: DW_FORM_addr

  // DIE 2:
  //   tag: DW_TAG_subprogram
  //   has_children: yes
  //   attributes:
  //     - name: DW_AT_name, form: DW_FORM_string
  //     - name: DW_AT_low_pc, form: DW_FORM_addr
  //     - name: DW_AT_high_pc, form: DW_FORM_addr

  // abbrev_code 1:
  // tag: DW_TAG_subprogram
  // has_children: yes
  // attributes:
  //   - name: DW_AT_name, form: DW_FORM_string
  //   - name: DW_AT_low_pc, form: DW_FORM_addr
  //   - name: DW_AT_high_pc, form: DW_FORM_addr

  // Use abbrev_table to get the abbrev.
  //   DIE 1:
  //   abbrev_code: 1
  //   attribute_values:
  //     - "function1"
  //     - 0x1000
  //     - 0x1100

  // DIE 2:
  //   abbrev_code: 1
  //   attribute_values:
  //     - "function2"
  //     - 0x2000
  //     - 0x2100
  const auto& abbrev = abbrev_table.at(abbrev_code);
  std::vector<const std::byte*> attr_locations;
  attr_locations.reserve(abbrev.attrs.size());
  for (auto& attr : abbrev.attrs) {
    attr_locations.emplace_back(cursor.positon());
    cursor.SkipForm(attr.form);
  }
  auto next = cursor.positon();
  return ldb::Die{die_start, &compile_unit, &abbrev, std::move(attr_locations),
                  next};
}
}  // namespace

const std::unordered_map<std::uint64_t, ldb::Abbrev>&
ldb::CompileUnit::abbrev_table() {
  return dwarf_->GetAbbrevTable(abbrev_offset_);
}

ldb::Die ldb::CompileUnit::root() {
  std::size_t header_size = 11;
  // Point to the first DIE.
  Cursor cursor{data_.subspan(header_size)};
  return ParseDie(*this, cursor);
}

ldb::Dwarf::Dwarf(const ldb::Elf& elf) : elf_{&elf} {
  compile_units_ = ParseCompileUnits(*this, elf);
}

const std::unordered_map<std::uint64_t, ldb::Abbrev>&
ldb::Dwarf::GetAbbrevTable(std::size_t offset) {
  if (!abbrev_tables_.contains(offset)) {
    abbrev_tables_.emplace(offset, ParseAbbrevTable(*elf_, offset));
  }
  return abbrev_tables_.at(offset);
}