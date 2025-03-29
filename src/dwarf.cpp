#include "libldb/detail/dwarf.h"

#include <algorithm>
#include <libldb/bit.hpp>
#include <libldb/dwarf.hpp>
#include <libldb/elf.hpp>
#include <libldb/error.hpp>
#include <span>
#include <format>
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

  bool Finished() const {
    return position_ >= std::to_address(std::end(data_));
  }

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
    const auto null_terminator =
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
    std::uint64_t result = 0;
    int shift = 0;
    std::uint8_t byte = 0;
    do {
      byte = s8();
      auto mask = static_cast<std::uint64_t>(byte & 0x7f);
      result |= mask << shift;
      shift += 7;
    } while ((byte & 0x80) != 0);

    if ((shift < sizeof(result) * 8) && (byte & 0x40) != 0) {
      result |= (~static_cast<std::uint64_t>(0) << shift);
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
        ldb::Error::Send(std::format("Unrecognized DWARF form: {}", form));
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
      dwarf, data, abbrev_offset);
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

// DIE 在 .debug_info 节中的结构非常紧凑，不包含 form 信息
ldb::Die ParseDie(const ldb::CompileUnit& compile_unit, Cursor cursor) {
  auto die_start = cursor.positon();
  auto abbrev_code = cursor.uleb128();
  if (abbrev_code == 0) {
    auto next = cursor.positon();
    return ldb::Die{next};
  }
  const auto& abbrev_table = compile_unit.abbrev_table();

  /* DIE Definition */
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

  /* Abbrev table Definition */
  // abbrev_code 1:
  // tag: DW_TAG_subprogram
  // has_children: yes
  // attributes:
  //   - name: DW_AT_name, form: DW_FORM_string
  //   - name: DW_AT_low_pc, form: DW_FORM_addr
  //   - name: DW_AT_high_pc, form: DW_FORM_addr

  /* Store structure */
  //   +----------------------+
  // | 缩写码(abbrev_code)  | ULEB128 编码
  // +----------------------+
  // | 属性值 1            | 格式取决于对应的 form
  // | 属性值 2            | 格式取决于对应的 form
  // | ...                 |
  // +----------------------+

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
  // 使用 find() 查找缩写码
  auto abbrev_it = abbrev_table.find(abbrev_code);

  // 检查是否找到了缩写码
  if (abbrev_it == abbrev_table.end()) {
    // 缩写码未找到，可以记录错误并返回一个无效的 DIE
    // 例如，使用 SPDLOG 或其他日志库:
    // SPDLOG_WARN("Unknown abbreviation code: {} in CU at offset {}", abbrev_code, ...);
    
    // 跳过这个无效的 DIE，或者返回一个表示错误的 DIE
    // 这里我们简单地返回一个标记结束的 DIE
    auto next = cursor.positon(); // 可能需要根据 DWARF 规范调整如何跳过
    return ldb::Die{next}; 
  }
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

ldb::FileAddr ldb::Attr::AsAddress() const {
  Cursor cursor{{location_, std::to_address(std::end(compile_unit_->data()))}};
  if (form_ != DW_FORM_addr) {
    Error::Send("Invalid address type");
  }
  auto elf = compile_unit_->dwarf()->elf();
  return FileAddr{*elf, cursor.u64()};
}

std::uint32_t ldb::Attr::AsSectionOffset() const {
  Cursor cursor{{location_, std::to_address(std::end(compile_unit_->data()))}};
  if (form_ != DW_FORM_sec_offset) {
    Error::Send("Invalid section offset type");
  }
  return cursor.u32();
}

std::uint64_t ldb::Attr::AsInt() const {
  Cursor cursor{{location_, std::to_address(std::end(compile_unit_->data()))}};
  switch (form_) {
    case DW_FORM_data1:
      return cursor.u8();
    case DW_FORM_data2:
      return cursor.u16();
    case DW_FORM_data4:
      return cursor.u32();
    case DW_FORM_data8:
      return cursor.u64();
    case DW_FORM_udata:
      return cursor.uleb128();
    default:
      Error::Send("Invalid integer type");
  }
}

std::span<const std::byte> ldb::Attr::AsBlock() const {
  std::size_t size;
  Cursor cursor{{location_, std::to_address(std::end(compile_unit_->data()))}};
  switch (form_) {
    case DW_FORM_block1:
      size = cursor.u8();
      break;
    case DW_FORM_block2:
      size = cursor.u16();
      break;
    case DW_FORM_block4:
      size = cursor.u32();
      break;
    case DW_FORM_block:
      size = cursor.uleb128();
      break;
    default:
      Error::Send("Invalid block type");
  }
  return {cursor.positon(), size};
}

ldb::Die ldb::Attr::AsReference() const {
  Cursor cursor{{location_, std::to_address(std::end(compile_unit_->data()))}};
  // The offset is relative to the compile unit start.
  std::size_t offset;
  switch (form_) {
    case DW_FORM_ref1:
      offset = cursor.u8();
      break;
    case DW_FORM_ref2:
      offset = cursor.u16();
      break;
    case DW_FORM_ref4:
      offset = cursor.u32();
      break;
    case DW_FORM_ref8:
      offset = cursor.u64();
      break;
    case DW_FORM_udata:
      offset = cursor.uleb128();
      break;
    case DW_FORM_ref_addr: {
      // DW_FORM_ref_addr 的偏移量是相对于整个 .debug_info 节的绝对偏移量
      offset = cursor.u32();
      auto section_content =
          compile_unit_->dwarf()->elf()->GetSectionContents(".debug_info");
      auto die_pos = std::next(std::begin(section_content), offset);
      const auto& compile_units = compile_unit_->dwarf()->compile_units();
      // 找到包含该DIE的编译单元
      auto cu_found = [=](const auto& cu) {
        return std::begin(cu->data()) <= die_pos &&
               die_pos < std::end(cu->data());
      };
      auto cu_for_offset = std::find_if(std::begin(compile_units), std::end(compile_units), cu_found);
      
      auto end = std::end(cu_for_offset->get()->data());
      Cursor ref_cursor{{std::to_address(die_pos), std::to_address(end)}};
      return ParseDie(**cu_for_offset, ref_cursor);
    }
    default:
      Error::Send("Invalid reference type");
  }
  Cursor ref_cursor{{std::next(std::begin(compile_unit_->data()), offset),
                     std::end(compile_unit_->data())}};
  return ParseDie(*compile_unit_, ref_cursor);
}

std::string_view ldb::Attr::AsString() const {
  Cursor cursor{{location_, std::to_address(std::end(compile_unit_->data()))}};
  switch (form_) {
    // 直接从当前位置读取 null 终止的字符串
    case DW_FORM_string:
      return cursor.string();
    // 存储一个 4 字节的偏移量，该偏移量指向 .debug_str 节中的位置
    case DW_FORM_strp: {
      auto offset = cursor.u32();
      auto section_content =
          compile_unit_->dwarf()->elf()->GetSectionContents(".debug_str");
      Cursor stab_cursor{{std::next(std::begin(section_content), offset),
                          std::end(section_content)}};
      return stab_cursor.string();
    }
    default:
      Error::Send("Invalid string type");
  }
}

ldb::RangeList ldb::Attr::AsRangeList() const {
  auto section_content =
      compile_unit_->dwarf()->elf()->GetSectionContents(".debug_ranges");
  // For DW_AT_ranges, the value is a section offset.
  auto offset = AsSectionOffset();
  std::span<const std::byte> data{std::begin(section_content) + offset,
                                  std::end(section_content)};

  // Root die contains DW_AT_low_pc and DW_AT_high_pc or DW_AT_ranges.
  auto root = compile_unit_->root();

  FileAddr base_address =
      root.Contains(DW_AT_low_pc) ? root[DW_AT_low_pc].AsAddress() : FileAddr{};
  return {compile_unit_, data, base_address};
}

const std::unordered_map<std::uint64_t, ldb::Abbrev>&
ldb::CompileUnit::abbrev_table() const {
  return dwarf_->GetAbbrevTable(abbrev_offset_);
}

ldb::Die ldb::CompileUnit::root() const {
  std::size_t header_size = 11;
  // Point to the first DIE.
  // Cursor cursor{data_.subspan(header_size)};
  Cursor cursor{{std::to_address(std::begin(data_) + header_size), std::to_address(std::end(data_))}};
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

const ldb::CompileUnit* ldb::Dwarf::CompileUnitContainingAddress(
    FileAddr address) const {
  for (const auto& cu : compile_units_) {
    if (cu->root().ContainsAddress(address)) {
      return cu.get();
    }
  }
  return nullptr;
}

std::optional<ldb::Die> ldb::Dwarf::FunctionContainingAddress(
    FileAddr address) const {
  Index();
  for (auto& [name, entry] : function_index_) {
    Cursor cursor{{entry.position,
                   std::to_address(std::end(entry.compile_unit->data()))}};
    auto d = ParseDie(*entry.compile_unit, cursor);
    if (d.ContainsAddress(address) &&
        d.abbrev_entry()->tag == DW_TAG_subprogram) {
      return d;
    }
  }
  return std::nullopt;
}

std::vector<ldb::Die> ldb::Dwarf::FindFunctions(std::string name) const {
  Index();
  std::vector<Die> found;
  auto [begin, end] = function_index_.equal_range(name);
  std::ranges::transform(begin, end, std::back_inserter(found), [](auto& p) {
    auto [name, entry] = p;
    Cursor cursor{{entry.position,
                   std::to_address(std::end(entry.compile_unit->data()))}};
    return ParseDie(*entry.compile_unit, cursor);
  });
  return found;
}

void ldb::Dwarf::Index() const {
  if (!function_index_.empty()) {
    return;
  }
  for (const auto& cu : compile_units_) {
    IndexDie(cu->root());
  }
}

void ldb::Dwarf::IndexDie(const ldb::Die& current) const {
  bool has_range =
      current.Contains(DW_AT_low_pc) || current.Contains(DW_AT_ranges);
  bool is_function = current.abbrev_entry()->tag == DW_TAG_subprogram ||
                     current.abbrev_entry()->tag == DW_TAG_inlined_subroutine;
  if (has_range && is_function) {
    if (auto name = current.Name(); name) {
      IndexEntry entry{current.compile_unit(), current.position()};
      function_index_.emplace(*name, entry);
    }
  }
  for (auto child : current.children()) {
    IndexDie(child);
  }
}

bool ldb::Die::Contains(std::uint64_t attribute) const {
  const auto& attr_specs = abbrev_entry_->attrs;
  return std::ranges::find_if(attr_specs, [=](const auto& attr_spec) {
           return attr_spec.attr == attribute;
         }) != std::end(attr_specs);
}

ldb::Attr ldb::Die::operator[](std::uint64_t attribute) const {
  const auto& attr_specs = abbrev_entry_->attrs;
  for (std::size_t i = 0; i < attr_specs.size(); i++) {
    if (attr_specs[i].attr == attribute) {
      return {compile_unit_, attr_specs[i].attr, attr_specs[i].form,
              attr_locations_[i]};
    }
  }
  Error::Send("Attribute not found");
}

ldb::FileAddr ldb::Die::LowPc() const {
  if (Contains(DW_AT_ranges)) {
    return operator[](DW_AT_ranges).AsRangeList().begin()->low;
  } else if (Contains(DW_AT_low_pc)) {
    return operator[](DW_AT_low_pc).AsAddress();
  }
  Error::Send("DIE does not have low pc");
}

ldb::FileAddr ldb::Die::HighPc() const {
  if (Contains(DW_AT_ranges)) {
    auto range = operator[](DW_AT_ranges).AsRangeList();
    auto begin = range.begin();
    while (std::next(begin) != std::end(range)) ++begin;
    return begin->high;

  } else if (Contains(DW_AT_high_pc)) {
    auto attr = operator[](DW_AT_high_pc);
    if (attr.form() == DW_FORM_addr) {
      // Absolute address.
      return attr.AsAddress();
    } else {
      // Relative to the low pc.
      return LowPc() + attr.AsInt();
    }
  }
  Error::Send("DIE does not have high pc");
}

bool ldb::Die::ContainsAddress(FileAddr addr) const {
  if (addr.elf() != compile_unit_->dwarf()->elf()) {
    return false;
  }
  if (Contains(DW_AT_ranges)) {
    return (*this)[DW_AT_ranges].AsRangeList().Contains(addr);
  } else if (Contains(DW_AT_low_pc)) {
    return LowPc() <= addr && addr < HighPc();
  }
  return false;
}

std::optional<std::string_view> ldb::Die::Name() const {
  // Function name
  if (Contains(DW_AT_name)) {
    return operator[](DW_AT_name).AsString();
  }
  // External name, reference to another DIE
  if (Contains(DW_AT_specification)) {
    return operator[](DW_AT_specification).AsReference().Name();
  }
  // Inline function, reference to another DIE
  if (Contains(DW_AT_abstract_origin)) {
    return operator[](DW_AT_abstract_origin).AsReference().Name();
  }
  return std::nullopt;
}

ldb::Die::ChildrenRange::iterator::iterator(const ldb::Die& die) {
  auto end = die.compile_unit()->data().end();
  Cursor next_cursor{{die.next(), std::to_address(end)}};
  die_ = ParseDie(*die.compile_unit_, next_cursor);
}

bool ldb::Die::ChildrenRange::iterator::operator==(const iterator& rhs) const {
  auto lhs_null = !die_.has_value() || !die_->abbrev_entry();
  auto rhs_null = !rhs.die_.has_value() || !rhs.die_->abbrev_entry();
  if (lhs_null && rhs_null) return true;
  if (lhs_null || rhs_null) return false;
  return die_->abbrev_entry() == rhs.die_->abbrev_entry() &&
         die_->next() == rhs->next();
}

ldb::Die::ChildrenRange::iterator&
ldb::Die::ChildrenRange::iterator::operator++() {
  if (!die_.has_value() || !die_->abbrev_entry()) return *this;
  if (!die_->abbrev_entry()->has_children) {
    // No children, just move to the next DIE.
    Cursor next_cursor{{die_->next(), std::to_address(std::end(
                                          die_->compile_unit()->data()))}};
    die_ = ParseDie(*die_->compile_unit_, next_cursor);
  } 
  // else if (die_->Contains(DW_AT_sibling)) {
  //   // 如果当前DIE包含DW_AT_sibling属性，则移动到下一个兄弟DIE
  //     auto& die = die_.value();
  //     auto attr = die[DW_AT_sibling];
  //       die_ = attr.AsReference();
  // } 
  else {
    // Has children, find the first child.
    iterator sub_children{*die_};
    // Skip all sub-children.
    while (sub_children->abbrev_entry_) ++sub_children;
    Cursor next_cursor{
        {sub_children->next(),
         std::to_address(std::end(die_->compile_unit()->data()))}};
    die_ = ParseDie(*die_->compile_unit_, next_cursor);
  }
  return *this;
}

ldb::Die::ChildrenRange::iterator ldb::Die::ChildrenRange::iterator::operator++(
    int) {
  auto tmp = *this;
  ++(*this);
  return tmp;
}

ldb::Die::ChildrenRange ldb::Die::children() const {
  return ChildrenRange{*this};
}

ldb::RangeList::iterator ldb::RangeList::begin() const {
  return {compile_unit_, data_, base_addr_};
}

ldb::RangeList::iterator ldb::RangeList::end() const { return {}; }

bool ldb::RangeList::Contains(FileAddr addr) const {
  return std::ranges::any_of(begin(), end(),
                             [=](const auto& e) { return e.Contains(addr); });
}

ldb::RangeList::iterator::iterator(const CompileUnit* cu,
                                   std::span<const std::byte> data,
                                   FileAddr base_addr)
    : compile_unit_{cu},
      data_{data},
      base_addr_{base_addr},
      position_{std::to_address(std::begin(data))} {
  ++(*this);
}

ldb::RangeList::iterator& ldb::RangeList::iterator::operator++() {
  // 0xFFFFFFFFFFFFFFFF 0x0000000000400000  // 基址选择器，设置基址为 0x400000
  // 0x0000000000000010 0x0000000000000020  // 范围 1：0x400010-0x400020
  // 0x0000000000000030 0x0000000000000040  // 范围 2：0x400030-0x400040
  // 0xFFFFFFFFFFFFFFFF 0x0000000000500000  // 新基址选择器，更改基址为 0x500000
  // 0x0000000000000005 0x0000000000000015  // 范围 3：0x500005-0x500015
  // 0x0000000000000000 0x0000000000000000  // 结束指示器
  auto elf = compile_unit_->dwarf()->elf();
  static constexpr auto base_addr_flag = ~static_cast<std::uint64_t>(0);

  Cursor cursor{{position_, std::to_address(std::end(data_))}};
  while (true) {
    current_.low = FileAddr{*elf, cursor.u64()};
    current_.high = FileAddr{*elf, cursor.u64()};
    if (current_.low.addr() == base_addr_flag) {
      base_addr_ = current_.high;
    } else if (current_.low.addr() == 0 && current_.high.addr() == 0) {
      position_ = nullptr;
      break;
    } else {
      position_ = cursor.positon();
      current_.low += base_addr_.addr();
      current_.high += base_addr_.addr();
      break;
    }
  }
  return *this;
}

ldb::RangeList::iterator ldb::RangeList::iterator::operator++(int) {
  auto tmp = *this;
  ++(*this);
  return tmp;
}
