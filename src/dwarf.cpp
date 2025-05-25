#include <algorithm>
#include <libldb/bit.hpp>
#include <libldb/dwarf.hpp>
#include <libldb/elf.hpp>
#include <libldb/error.hpp>
#include <libldb/types.hpp>
#include <ranges>
#include <span>

namespace {
bool PathEndsIn(const std::filesystem::path& lhs, const std::filesystem::path& rhs) {
  auto lhsSize = std::ranges::distance(lhs);
  auto rhsSize = std::ranges::distance(rhs);
  if (rhsSize > lhsSize) return false;
  auto start = std::ranges::next(lhs.begin(), lhsSize - rhsSize);
  return std::equal(start, lhs.end(), rhs.begin());
}
class Cursor {
 public:
  explicit Cursor(ldb::Span<const std::byte> _data) : data(_data), pos(_data.Begin()) {}

  Cursor& operator++() {
    ++pos;
    return *this;
  }

  Cursor& operator+=(std::size_t n) {
    pos += n;
    return *this;
  }

  const std::byte* Position() { return pos; }

  bool Finished() const { return pos >= data.End(); }

  template <typename T>
  T FixInt() {
    auto t = ldb::FromBytes<T>(pos);
    pos += sizeof(T);
    return t;
  }

  std::uint8_t U8() { return FixInt<std::uint8_t>(); }

  std::uint16_t U16() { return FixInt<std::uint16_t>(); }

  std::uint32_t U32() { return FixInt<std::uint32_t>(); }

  std::uint64_t U64() { return FixInt<std::uint64_t>(); }

  std::int8_t S8() { return FixInt<std::int8_t>(); }

  std::int16_t S16() { return FixInt<std::int16_t>(); }

  std::int32_t S32() { return FixInt<std::int32_t>(); }

  std::int64_t S64() { return FixInt<std::int64_t>(); }

  std::string_view String() {
    auto null = std::find(pos, data.End(), std::byte{0});
    std::string_view ret{reinterpret_cast<const char*>(pos), static_cast<std::size_t>(null - pos)};
    pos = null + 1;
    return ret;
  }

  // 小端存储
  // 编码整数624,485

  // 二进制表示：100110 0001110 1100101

  // 补齐到7的倍数位数：010011000011101100101

  // 拆分为7位组：0100110 0001110 1100101

  // 加前缀位：00100110 10001110 11100101

  // 有符号整数编码（SLEB128）：

  // 先用二补码表示整数，位宽为7的倍数。

  // 然后按ULEB128相同方式拆分和加前缀。
  std::uint64_t Uleb128() {
    std::uint64_t ret = 0;
    int shift = 0;
    std::uint8_t byte = 0;
    do {
      byte = U8();
      auto mask = static_cast<std::uint64_t>(byte & 0x7f);
      ret |= mask << shift;
      shift += 7;
    } while ((byte & 0x80) != 0);
    return ret;
  }

  // 举例：编码-123,456

  // 123,456二进制：11110001001000000

  // 补齐到7的倍数：000011110001001000000

  // 取反：111100001110110111111

  // 加1：111100001110111000000

  // 拆分7位组：1111000 0111011 1000000

  // 加前缀位：01111000 10111011 11000000
  std::int64_t Sleb128() {
    std::uint64_t ret = 0;
    int shift = 0;
    std::uint8_t byte = 0;
    do {
      byte = U8();
      auto mask = static_cast<std::uint64_t>(byte & 0x7f);
      ret |= mask << shift;
      shift += 7;
    } while ((byte & 0x80) != 0);

    if (shift < 64 && (byte & 0x40) != 0) {
      ret |= (~static_cast<std::uint64_t>(0) << shift);
    }
    return static_cast<std::int64_t>(ret);
  }

  void SkipForm(std::uint64_t form) {
    switch (form) {
      case DW_FORM_flag_present:
        break;

      case DW_FORM_data1:
      case DW_FORM_ref1:
      case DW_FORM_flag:
        pos += 1;
        break;

      case DW_FORM_data2:
      case DW_FORM_ref2:
        pos += 2;
        break;

      case DW_FORM_data4:
      case DW_FORM_ref4:
      case DW_FORM_ref_addr:
      case DW_FORM_sec_offset:
      case DW_FORM_strp:
        pos += 4;
        break;

      case DW_FORM_data8:
      case DW_FORM_addr:
        pos += 8;
        break;

      case DW_FORM_sdata:
        Sleb128();

      case DW_FORM_udata:
      case DW_FORM_ref_udata:
        Uleb128();
        break;

      case DW_FORM_block1:
        pos += U8();
        break;
      case DW_FORM_block2:
        pos += U16();
        break;
      case DW_FORM_block4:
        pos += U32();
        break;
      case DW_FORM_block:
      case DW_FORM_exprloc:
        pos += Uleb128();
        break;
      case DW_FORM_string:
        while (!Finished() && *pos != std::byte{0}) ++pos;
        ++pos;
        break;
      case DW_FORM_indirect:
        SkipForm(Uleb128());
        break;

      default:
        ldb::Error::Send("Unrecognized DWARF form");
    }
  }

 private:
  ldb::Span<const std::byte> data;
  const std::byte* pos;
};

std::unordered_map<std::uint64_t, ldb::Abbrev> ParseAbbrevTable(const ldb::Elf& obj, std::size_t offset) {
  Cursor cursor{obj.GetSectionContents(".debug_abbrev")};
  cursor += offset;

  std::unordered_map<std::uint64_t, ldb::Abbrev> table;
  std::uint64_t code = 0;
  do {
    code = cursor.Uleb128();
    auto tag = cursor.Uleb128();
    auto hasChildren = cursor.U8() == 1 ? true : false;
    std::vector<ldb::AttrSpec> attrSpecs;
    std::uint64_t attr = 0;
    std::uint64_t form = 0;
    do {
      attr = cursor.Uleb128();
      form = cursor.Uleb128();
      if (attr != 0 && form != 0) {
        attrSpecs.emplace_back(attr, form);
      }
    } while (attr != 0 && form != 0);

    if (code != 0) {
      table.emplace(code, ldb::Abbrev{code, tag, hasChildren, std::move(attrSpecs)});
    }
  } while (code != 0);
  return table;
}

std::unique_ptr<ldb::CompileUnit> ParseCompileUnit(ldb::Dwarf& dwarf, const ldb::Elf& obj, Cursor cursor) {
  auto start = cursor.Position();
  auto size = cursor.U32();
  auto version = cursor.U16();
  auto abbrevOffset = cursor.U32();
  auto addrSize = cursor.U8();
  if (size == 0xffffffff) {
    ldb::Error::Send("Only DWARF32 is supported");
  }
  if (version != 4) {
    ldb::Error::Send("Only DWARF version 4 is supported");
  }
  if (addrSize != 8) {
    ldb::Error::Send("Invalid address size of DWARF");
  }

  size += sizeof(std::uint32_t);
  ldb::Span<const std::byte> data = {start, size};
  return std::make_unique<ldb::CompileUnit>(dwarf, data, abbrevOffset);
}

std::vector<std::unique_ptr<ldb::CompileUnit>> ParseCompileUnits(ldb::Dwarf& dwarf, const ldb::Elf& obj) {
  auto debugInfo = obj.GetSectionContents(".debug_info");
  Cursor cursor{debugInfo};

  std::vector<std::unique_ptr<ldb::CompileUnit>> units;
  while (!cursor.Finished()) {
    auto unit = ParseCompileUnit(dwarf, obj, cursor);
    cursor += unit->Data().Size();
    units.push_back(std::move(unit));
  }
  return units;
}

ldb::Die ParseDie(const ldb::CompileUnit& compileUnit, Cursor cursor) {
  auto pos = cursor.Position();
  auto abbrevCode = cursor.Uleb128();
  if (abbrevCode == 0) {
    auto next = cursor.Position();
    return ldb::Die{next};
  }
  auto& abbrevTable = compileUnit.AbbrevTable();
  auto& abbrevEntry = abbrevTable.at(abbrevCode);

  std::vector<const std::byte*> attrLocs;
  attrLocs.reserve(abbrevEntry.attrSpecs.size());
  for (const auto& attr : abbrevEntry.attrSpecs) {
    attrLocs.push_back(cursor.Position());
    cursor.SkipForm(attr.form);
  }
  auto next = cursor.Position();
  return ldb::Die{pos, &compileUnit, &abbrevEntry, std::move(attrLocs), next};
}

ldb::LineTable::File ParseLineTableFile(Cursor& cursor, std::filesystem::path compilationDir,
                                        std::span<const std::filesystem::path> includeDirectories) {
  auto file = cursor.String();
  auto dirIndex = cursor.Uleb128();
  auto modificationTime = cursor.Uleb128();
  auto fileLength = cursor.Uleb128();

  std::filesystem::path path = file;
  if (!file.starts_with('/')) {
    if (dirIndex == 0) {
      path = compilationDir / file;
    } else {
      path = includeDirectories[dirIndex - 1] / file;
    }
  }
  return {path.string(), modificationTime, fileLength};
}

std::unique_ptr<ldb::LineTable> ParseLineTable(const ldb::CompileUnit& cu) {
  auto section = cu.DwarfInfo()->ElfFile()->GetSectionContents(".debug_line");
  if (!cu.Root().Contains(DW_AT_stmt_list)) return nullptr;
  auto offset = cu.Root()[DW_AT_stmt_list].AsSectionOffset();
  Cursor cursor{{section.Begin() + offset, section.End()}};

  // parse header
  auto size = cursor.U32();
  auto end = cursor.Position() + size;
  auto version = cursor.U16();
  if (version != 4) ldb::Error::Send("Only DWARF 4 is supported");
  cursor.U32();
  auto minimumInstructionLength = cursor.U8();
  if (minimumInstructionLength != 1) {
    ldb::Error::Send("Invalid minimum instruction length");
  }
  auto maximumOperationsPerInstruction = cursor.U8();
  if (maximumOperationsPerInstruction != 1) {
    ldb::Error::Send("invalid maximum operations per instruction");
  }
  auto defaultIsStmt = cursor.U8();
  auto lineBase = cursor.S8();
  auto lineRange = cursor.U8();
  auto opcodeBase = cursor.U8();
  std::array<std::uint8_t, 12> expectedOpcodeLengths{0, 1, 1, 1, 1, 0, 0, 0, 1, 0, 0, 1};
  for (int i = 0; i < opcodeBase - 1; i++) {
    if (cursor.U8() != expectedOpcodeLengths[i]) {
      ldb::Error::Send("Unexpected opcode length");
    }
  }

  std::vector<std::filesystem::path> includeDirectories;
  std::filesystem::path compilationDir{cu.Root()[DW_AT_comp_dir].AsString()};
  for (auto dir = cursor.String(); !dir.empty(); dir = cursor.String()) {
    if (dir.starts_with('/')) {
      includeDirectories.push_back(dir);
    } else {
      includeDirectories.push_back(compilationDir / dir);
    }
  }
  std::vector<ldb::LineTable::File> fileNames;
  while (*cursor.Position() != std::byte{0}) {
    fileNames.push_back(ParseLineTableFile(cursor, compilationDir, includeDirectories));
  }
  cursor += 1;
  ldb::Span<const std::byte> data{cursor.Position(), end};
  return std::make_unique<ldb::LineTable>(data, &cu, defaultIsStmt, lineBase, lineRange, opcodeBase,
                                          std::move(includeDirectories), std::move(fileNames));
}

std::uint64_t ParseEhFramePointerWithBase(Cursor& cursor, std::uint8_t encoding, std::uint64_t base) {
  switch (encoding & 0x0f) {
    case DW_EH_PE_absptr:
      return base + cursor.U64();
    case DW_EH_PE_uleb128:
      return base + cursor.Uleb128();
    case DW_EH_PE_udata2:
      return base + cursor.U16();
    case DW_EH_PE_udata4:
      return base + cursor.U32();
    case DW_EH_PE_udata8:
      return base + cursor.U64();
    case DW_EH_PE_sleb128:
      return base + cursor.Sleb128();
    case DW_EH_PE_sdata2:
      return base + cursor.S16();
    case DW_EH_PE_sdata4:
      return base + cursor.S32();
    case DW_EH_PE_sdata8:
      return base + cursor.S64();
    default:
      ldb::Error::Send("Invalid eh frame pointer encoding");
  }
}

/// 解析 eh_frame 中的指针
/// func_start 是当前FDE对应的函数的首地址
std::uint64_t ParseEhFramePointer(const ldb::Elf& elf, Cursor& cursor, std::uint8_t encoding, std::uint64_t pc,
                                  std::uint64_t text_section_start, std::uint64_t data_section_start,
                                  std::uint64_t func_start) {
  std::uint64_t base = 0;
  switch (encoding & 0x07) {
    case DW_EH_PE_absptr:  // 绝对地址
      break;
    case DW_EH_PE_pcrel:  // 相对地址
      base = pc;
      break;
    case DW_EH_PE_textrel:  // 相对 text 段
      base = text_section_start;
      break;
    case DW_EH_PE_datarel:  // 相对 data 段
      base = data_section_start;
      break;
    case DW_EH_PE_funcrel:  // 相对函数起始地址
      base = func_start;
      break;
    default:
      ldb::Error::Send("Invalid eh frame pointer encoding");
  }
  return ParseEhFramePointerWithBase(cursor, encoding, base);
}

ldb::CallFrameInformation::CommonInformationEntry ParseCIE(Cursor cursor) {
  auto start = cursor.Position();
  // +4包含自身
  auto length = cursor.U32() + 4;
  // 区分 CIE / FDE：
  //  1.debug_frame = 0xffffffff
  //  2.eh_frame = 0x00000000
  auto id = cursor.U32();
  auto version = cursor.U8();
  if (version != 1 && version != 3 && version != 4) {
    ldb::Error::Send("Invalid version of CIE");
  }
  auto augmentation = cursor.String();
  if (!augmentation.empty() && !augmentation.starts_with('z')) {
    ldb::Error::Send("Invalid CIE augmentation string (must begin with 'z' or be empty)");
  }

  if (version == 4) {
    auto address_size = cursor.U8();
    auto segment_size = cursor.U8();
    if (address_size != 8) {
      ldb::Error::Send("Invalid address size of CIE");
    }
    if (segment_size != 0) {
      ldb::Error::Send("Invalid segment size of CIE");
    }
  }

  // 供 DW_CFA_advance_loc：指令偏移 × CAF = 字节数。
  auto code_alignment_factor = cursor.Uleb128();
  // 调整栈向上/向下方向；x86-64 上通常 = -8。
  auto data_alignment_factor = cursor.Sleb128();
  // 返回地址所在寄存器号；x86-64 = 16（RIP）。DWARF 2 用 1 B，DWARF 3+ 用 ULEB128。
  auto return_address_register = version == 1 ? cursor.U8() : cursor.Uleb128();

  std::uint8_t fde_pointer_encoding = DW_EH_PE_absptr | DW_EH_PE_udata8;
  for (auto c : augmentation) {
    switch (c) {
      case 'z':
        cursor.Uleb128();
        break;
      case 'R':
        // 相对initial_location 地址的偏移
        fde_pointer_encoding = cursor.U8();
        break;
      case 'L':
        cursor.U8();
        break;
      case 'P': {
        auto encoding = cursor.U8();
        ParseEhFramePointerWithBase(cursor, encoding, 0);
        break;
      }
      default:
        ldb::Error::Send("Invalid CIE augmentation string");
    }
  }
  ldb::Span<const std::byte> instructions{cursor.Position(), start + length};
  bool fde_has_augmentation = !augmentation.empty();
  return {length,      code_alignment_factor, data_alignment_factor, fde_has_augmentation, fde_pointer_encoding,
          instructions};
}

ldb::CallFrameInformation::FrameDescriptionEntry ParseFDE(const ldb::CallFrameInformation& cfi, Cursor cursor) {
  auto start = cursor.Position();
  auto length = cursor.U32() + 4;
  // 负偏移量（s32）：从本字节 向前回溯到配对的 CIE 起始位置。
  // 若 .debug_frame，此字段恒为 正偏移 (0)；但在常见的 .eh_frame 格式里，它是负数。
  auto elf = cfi.DwarfInfo().ElfFile();
  auto current_offset = elf->DataPointerAsFileOffset(cursor.Position());
  ldb::FileOffset cie_offset{*elf, current_offset.Offset() - cursor.U32()};
  auto& cie = cfi.GetCIE(cie_offset);
  current_offset = elf->DataPointerAsFileOffset(cursor.Position());
  auto text_section_start = elf->GetSectonStartAddress(".text").value_or(ldb::FileAddr{});
  auto initial_location_addr = ParseEhFramePointer(*elf, cursor, cie.fde_pointer_encoding, current_offset.Offset(),
                                                   text_section_start.Addr(), 0, 0);
  ldb::FileAddr initial_location{*elf, initial_location_addr};
  auto address_range = ParseEhFramePointerWithBase(cursor, cie.fde_pointer_encoding, 0);
  if (cie.fde_has_augmentation) {
    auto aug_len = cursor.Uleb128();
    cursor += aug_len;
  }
  ldb::Span<const std::byte> instructions{cursor.Position(), start + length};
  return {length, &cie, initial_location, address_range, instructions};
}

/// 要想随机跳转到任意表项，需要知道“每条表项占多少字节”。
/// 字节大小由 .eh_frame_hdr 里的 table_enc 指定——下面这段函数用来根据编码计算大小：
std::size_t EhFramePointerEncodingSize(std::uint8_t encoding) {
  switch (encoding & 0x07) {
    case DW_EH_PE_absptr:
      return 8;
    case DW_EH_PE_udata2:
      return 2;
    case DW_EH_PE_udata4:
      return 4;
    case DW_EH_PE_udata8:
      return 8;
  }
  ldb::Error::Send("Invalid eh frame pointer encoding");
}

ldb::CallFrameInformation::EhHdr ParseEhHdr(ldb::Dwarf& dwarf) {
  auto elf = dwarf.ElfFile();

  auto eh_hdr_start = *elf->GetSectonStartAddress(".eh_frame_hdr");
  auto text_section_start = *elf->GetSectonStartAddress(".text");

  auto eh_hdr_data = elf->GetSectionContents(".eh_frame_hdr");
  Cursor cursor{eh_hdr_data};
  auto start = cursor.Position();
  auto version = cursor.U8();
  // 指向.eh_frame节的指针编码
  auto eh_frame_ptr_enc = cursor.U8();
  // FDE 数编码
  auto fde_count_enc = cursor.U8();
  // 查找表编码
  auto table_enc = cursor.U8();
  // 解码eh_frame指针
  ParseEhFramePointerWithBase(cursor, eh_frame_ptr_enc, 0);
  // 解码FDE数量
  auto fde_count = ParseEhFramePointerWithBase(cursor, fde_count_enc, 0);
  // 查找表的开始位置
  auto search_table = cursor.Position();
  return {start, search_table, fde_count, table_enc, nullptr};
}
}  // namespace

namespace ldb {

CompileUnit::CompileUnit(Dwarf& _parent, Span<const std::byte> _data, std::size_t _abbrevOffset)
    : parent(&_parent), data(_data), abbrevOffset(_abbrevOffset) {
  lineTable = ParseLineTable(*this);
}

const std::unordered_map<std::uint64_t, Abbrev>& CompileUnit::AbbrevTable() const {
  return parent->GetAbbrevTable(abbrevOffset);
}

Die CompileUnit::Root() const {
  std::size_t headerSize = 11;
  Cursor cursor{{data.Begin() + headerSize, data.End()}};
  return ParseDie(*this, cursor);
}

Dwarf::Dwarf(const Elf& parent) : elf(&parent) { compileUnits = ParseCompileUnits(*this, parent); }

const std::unordered_map<std::uint64_t, Abbrev>& Dwarf::GetAbbrevTable(std::size_t offset) {
  if (!abbrevTables.contains(offset)) {
    abbrevTables.emplace(offset, ParseAbbrevTable(*elf, offset));
  }
  return abbrevTables.at(offset);
}

const CompileUnit* Dwarf::CompileUnitContainingAddress(FileAddr address) const {
  if (auto it =
          std::ranges::find_if(compileUnits, [address](const auto& cu) { return cu->Root().ContainsAddress(address); });
      it != std::end(compileUnits)) {
    return it->get();
  }
  return nullptr;
}

std::optional<Die> Dwarf::FunctionContainingAddress(FileAddr address) const {
  Index();
  for (const auto& [name, entry] : functionIndex) {
    Cursor cursor{{entry.pos, entry.compileUnit->Data().End()}};
    auto die = ParseDie(*entry.compileUnit, cursor);
    if (die.ContainsAddress(address) && die.AbbrevEntry()->tag == DW_TAG_subprogram) return die;
  }
  return std::nullopt;
}

std::vector<Die> Dwarf::FindFunctions(std::string name) const {
  Index();
  std::vector<Die> ret;
  auto [begin, end] = functionIndex.equal_range(name);
  std::transform(begin, end, std::back_inserter(ret), [](const auto& p) {
    auto [name, entry] = p;
    Cursor cursor{{entry.pos, entry.compileUnit->Data().End()}};
    return ParseDie(*entry.compileUnit, cursor);
  });
  return ret;
}

std::vector<Die> Dwarf::InlineStackAtAddress(FileAddr address) const {
  auto func = FunctionContainingAddress(address);
  std::vector<Die> stack;
  if (func) {
    stack.push_back(*func);
    while (true) {
      const auto& child = stack.back().Children();
      auto found = std::ranges::find_if(child, [=](auto& child) {
        return child.AbbrevEntry()->tag == DW_TAG_inlined_subroutine && child.ContainsAddress(address);
      });
      if (found == child.end())
        break;
      else
        stack.push_back(*found);
    }
  }
  return stack;
}

void Dwarf::Index() const {
  if (!functionIndex.empty()) return;
  std::ranges::for_each(compileUnits, [this](auto& cu) { IndexDie(cu->Root()); });
}

void Dwarf::IndexDie(const Die& current) const {
  bool hasRange = current.Contains(DW_AT_ranges) || current.Contains(DW_AT_low_pc);
  bool isFunc =
      current.AbbrevEntry()->tag == DW_TAG_subprogram || current.AbbrevEntry()->tag == DW_TAG_inlined_subroutine;
  if (hasRange && isFunc) {
    if (auto name = current.Name(); name) {
      IndexEntry entry{current.Cu(), current.Position()};
      functionIndex.emplace(*name, entry);
    }
  }
  for (auto child : current.Children()) {
    IndexDie(child);
  }
}

std::optional<std::string_view> Die::Name() const {
  if (Contains(DW_AT_name)) {
    return (*this)[DW_AT_name].AsString();
  }
  if (Contains(DW_AT_specification)) {
    // func define
    return (*this)[DW_AT_specification].AsReference().Name();
  }
  if (Contains(DW_AT_abstract_origin)) {
    // inline func
    return (*this)[DW_AT_abstract_origin].AsReference().Name();
  }
  return std::nullopt;
}

bool Die::Contains(std::uint64_t attribute) const {
  return std::ranges::find_if(abbrev->attrSpecs, [=](const auto spec) { return spec.attr == attribute; }) !=
         std::end(abbrev->attrSpecs);
}

Attr Die::operator[](std::uint64_t attribute) const {
  auto& specs = abbrev->attrSpecs;
  for (int i = 0; i < specs.size(); i++) {
    if (specs[i].attr == attribute) {
      return {compileUnit, specs[i].attr, specs[i].form, attrLocations[i]};
    }
  }
  Error::Send("Attribute not found");
}

FileAddr Die::LowPc() const {
  if (Contains(DW_AT_ranges)) {
    auto firstEntry = (*this)[DW_AT_ranges].AsRangeList().begin();
    return firstEntry->low;
  } else if (Contains(DW_AT_low_pc)) {
    return (*this)[DW_AT_low_pc].AsAddress();
  }
  Error::Send("DIE does not have low PC");
}

FileAddr Die::HighPc() const {
  if (Contains(DW_AT_ranges)) {
    auto ranges = (*this)[DW_AT_ranges].AsRangeList();
    auto it = ranges.begin();
    while (std::next(it) != ranges.end()) ++it;
    return it->high;
  } else if (Contains(DW_AT_high_pc)) {
    auto attr = (*this)[DW_AT_high_pc];
    if (attr.Form() == DW_FORM_addr) {
      return attr.AsAddress();
    } else {
      return LowPc() + attr.AsInt();
    }
  }
  Error::Send("DIE does not have high PC");
}

bool Die::ContainsAddress(FileAddr address) const {
  if (address.ElfFile() != compileUnit->DwarfInfo()->ElfFile()) return false;

  if (Contains(DW_AT_ranges)) {
    return (*this)[DW_AT_ranges].AsRangeList().Contains(address);
  } else if (Contains(DW_AT_low_pc)) {
    return LowPc() <= address && address < HighPc();
  }
  return false;
}

SourceLoaction Die::Location() const { return {&File(), Line()}; }

const LineTable::File& Die::File() const {
  std::uint64_t fileIndex;
  if (abbrev->tag == DW_TAG_inlined_subroutine) {
    fileIndex = (*this)[DW_AT_call_file].AsInt();
  } else {
    fileIndex = (*this)[DW_AT_decl_file].AsInt();
  }
  return this->compileUnit->Lines().FileNames()[fileIndex - 1];
}

std::uint64_t Die::Line() const {
  if (abbrev->tag == DW_TAG_inlined_subroutine) {
    return (*this)[DW_AT_call_line].AsInt();
  }
  return (*this)[DW_AT_decl_line].AsInt();
}

Die::ChildrenRange Die::Children() const { return ChildrenRange{*this}; }

Die::ChildrenRange::iterator::iterator(const Die& _die) {
  Cursor nextCur{{_die.Next(), _die.compileUnit->Data().End()}};
  die = ParseDie(*_die.compileUnit, nextCur);
}

bool Die::ChildrenRange::iterator::operator==(const iterator& rhs) const {
  auto lhsNull = !die.has_value() || !die->AbbrevEntry();
  auto rhsNull = !rhs.die.has_value() || !rhs.die->AbbrevEntry();
  if (lhsNull && rhsNull) return true;
  if (lhsNull || rhsNull) return false;

  return die->abbrev == rhs->abbrev && die->Next() == rhs->Next();
}

Die::ChildrenRange::iterator& Die::ChildrenRange::iterator::operator++() {
  if (!die.has_value() || !die->abbrev) return *this;
  if (!die->abbrev->hasChildren) {
    Cursor nextCur{{die->next, die->compileUnit->Data().End()}};
    die = ParseDie(*die->compileUnit, nextCur);
  } else if (die->Contains(DW_AT_sibling)) {
    die = die.value()[DW_AT_sibling].AsReference();
  } else {
    iterator subChild{*die};
    while (subChild->abbrev) ++subChild;
    Cursor nextCur{{subChild->next, die->compileUnit->Data().End()}};
    die = ParseDie(*die->compileUnit, nextCur);
  }
  return *this;
}

Die::ChildrenRange::iterator Die::ChildrenRange::iterator::operator++(int) {
  auto t = *this;
  ++(*this);
  return t;
}

FileAddr Attr::AsAddress() const {
  Cursor cursor{{location, compileUnit->Data().End()}};
  if (form != DW_FORM_addr) Error::Send("Invalid address type");
  auto elf = compileUnit->DwarfInfo()->ElfFile();
  return {*elf, cursor.U64()};
}

std::uint32_t Attr::AsSectionOffset() const {
  Cursor cursor{{location, compileUnit->Data().End()}};
  if (form != DW_FORM_sec_offset) Error::Send("Invalid offset type");
  // only support 32bit
  return cursor.U32();
}

Span<const std::byte> Attr::AsBlock() const {
  Cursor cursor{{location, compileUnit->Data().End()}};
  std::size_t size;
  switch (form) {
    case DW_FORM_block1:
      size = cursor.U8();
      break;
    case DW_FORM_block2:
      size = cursor.U16();
      break;
    case DW_FORM_block4:
      size = cursor.U32();
      break;
    case DW_FORM_block:
      size = cursor.Uleb128();
      break;
    default:
      Error::Send("Invalid block type");
  }
  return {cursor.Position(), size};
}

std::uint64_t Attr::AsInt() const {
  Cursor cursor{{location, compileUnit->Data().End()}};
  switch (form) {
    case DW_FORM_data1:
      return cursor.U8();
    case DW_FORM_data2:
      return cursor.U16();
    case DW_FORM_data4:
      return cursor.U32();
    case DW_FORM_data8:
      return cursor.U64();
    case DW_FORM_udata:
      return cursor.Uleb128();
    default:
      Error::Send("Invalid integer type");
  }
}

std::string_view Attr::AsString() const {
  Cursor cursor{{location, compileUnit->Data().End()}};
  switch (form) {
    case DW_FORM_string:
      return cursor.String();
    case DW_FORM_strp: {
      auto offset = cursor.U32();
      auto section = compileUnit->DwarfInfo()->ElfFile()->GetSectionContents(".debug_str");
      Cursor cursor{{section.Begin() + offset, section.End()}};
      return cursor.String();
    }
    default:
      Error::Send("Invalid string type");
  }
}

Die Attr::AsReference() const {
  Cursor cursor{{location, compileUnit->Data().End()}};
  std::size_t offset;
  switch (form) {
    case DW_FORM_ref1:
      offset = cursor.U8();
      break;
    case DW_FORM_ref2:
      offset = cursor.U16();
      break;
    case DW_FORM_ref4:
      offset = cursor.U32();
      break;
    case DW_FORM_ref8:
      offset = cursor.U64();
      break;
    case DW_FORM_ref_udata:
      offset = cursor.Uleb128();
      break;
    case DW_FORM_ref_addr:
      // can reference data in other compile units
      // so its offset is relative to the start of `.debug_info` section
      {
        offset = cursor.U32();
        auto section = compileUnit->DwarfInfo()->ElfFile()->GetSectionContents(".debug_info");
        auto diePos = section.Begin() + offset;
        auto& compileUnits = compileUnit->DwarfInfo()->CompileUnits();
        auto belongs = std::ranges::find_if(compileUnits, [diePos](const auto& cu) {
          return cu->Data().Begin() <= diePos && diePos < cu->Data().End();
        });
        Cursor refCursor{{diePos, belongs->get()->Data().End()}};
        return ParseDie(*belongs->get(), refCursor);
      }
    default:
      Error::Send("Invalid reference type");
  }
  Cursor refCursor{{compileUnit->Data().Begin() + offset, compileUnit->Data().End()}};
  return ParseDie(*compileUnit, refCursor);
}

RangeList Attr::AsRangeList() const {
  auto section = compileUnit->DwarfInfo()->ElfFile()->GetSectionContents(".debug_ranges");
  auto offset = AsSectionOffset();
  Span<const std::byte> data{section.Begin() + offset, section.End()};
  auto root = compileUnit->Root();
  FileAddr baseAddress = root.Contains(DW_AT_low_pc) ? root[DW_AT_low_pc].AsAddress() : FileAddr{};
  return {compileUnit, data, baseAddress};
}

RangeList::iterator::iterator(const CompileUnit* _compileUnit, Span<const std::byte> _data, FileAddr _baseAddress)
    : compileUnit(_compileUnit), data(_data), baseAddress(_baseAddress), pos(_data.Begin()) {
  ++(*this);
}

RangeList::iterator& RangeList::iterator::operator++() {
  auto elf = compileUnit->DwarfInfo()->ElfFile();
  constexpr auto baseAddressFlag = ~static_cast<std::uint64_t>(0);
  Cursor cursor{{pos, data.End()}};
  while (true) {
    current.low = FileAddr{*elf, cursor.U64()};
    current.high = FileAddr{*elf, cursor.U64()};
    if (current.low.Addr() == baseAddressFlag) {
      baseAddress = current.high;
    } else if (current.low.Addr() == 0 && current.high.Addr() == 0) {
      pos = nullptr;
      break;
    } else {
      pos = cursor.Position();
      current.low += baseAddress.Addr();
      current.high += baseAddress.Addr();
      break;
    }
  }
  return *this;
}

RangeList::iterator RangeList::iterator::operator++(int) {
  auto t = *this;
  ++(*this);
  return t;
}

RangeList::iterator RangeList::begin() const { return {compileUnit, data, baseAddr}; }

RangeList::iterator RangeList::end() const { return {}; }

bool RangeList::Contains(FileAddr addr) const {
  return std::ranges::any_of(*this, [addr](const auto& e) { return e.Contains(addr); });
}

LineTable::iterator LineTable::begin() const { return iterator(this); }

LineTable::iterator LineTable::end() const { return {}; }

LineTable::iterator::iterator(const LineTable* _table) : table(_table), pos(table->data.Begin()) {
  registers.isStmt = table->defaultIsStmt;
  ++(*this);
}

LineTable::iterator& LineTable::iterator::operator++() {
  if (pos == table->data.End()) {
    pos = nullptr;
    return *this;
  }
  bool emitted = false;
  do {
    emitted = ExecuteInstruction();
  } while (!emitted);

  current.fileEntry = &table->fileNames[current.fileIndex - 1];
  return *this;
}

LineTable::iterator LineTable::iterator::operator++(int) {
  auto t = *this;
  ++(*this);
  return t;
}

bool LineTable::iterator::ExecuteInstruction() {
  auto elf = table->Cu().DwarfInfo()->ElfFile();
  Cursor cursor{{pos, table->data.End()}};
  auto opcode = cursor.U8();
  bool emitted = false;
  // standard op
  if (opcode > 0 && opcode < table->opcodeBase) {
    switch (opcode) {
      case DW_LNS_copy:
        current = registers;
        registers.basicBlockStart = false;
        registers.prologueEnd = false;
        registers.epilogueBegin = false;
        registers.discriminator = 0;
        emitted = true;
        break;
      case DW_LNS_advance_pc:
        registers.address += cursor.Uleb128();
        break;
      case DW_LNS_advance_line:
        registers.line += cursor.Sleb128();
        break;
      case DW_LNS_set_file:
        registers.fileIndex = cursor.Uleb128();
        break;
      case DW_LNS_set_column:
        registers.column = cursor.Uleb128();
        break;
      case DW_LNS_negate_stmt:
        registers.isStmt = !registers.isStmt;
        break;
      case DW_LNS_set_basic_block:
        registers.basicBlockStart = true;
        break;
      case DW_LNS_const_add_pc:
        registers.address += (255 - table->opcodeBase) / table->lineRange;
        break;
      case DW_LNS_fixed_advance_pc:
        registers.address += cursor.U16();
        break;
      case DW_LNS_set_prologue_end:
        registers.prologueEnd = true;
        break;
      case DW_LNS_set_epilogue_begin:
        registers.epilogueBegin = true;
        break;
      case DW_LNS_set_isa:
        break;
      default:
        Error::Send("Unexpected standard opcode");
    }
  } else if (opcode == 0) {
    // extended opcodes
    auto length = cursor.Uleb128();
    auto extendedOpcode = cursor.U8();
    switch (extendedOpcode) {
      case DW_LNE_end_sequence:
        registers.endSequence = true;
        current = registers;
        registers = {};
        registers.isStmt = table->defaultIsStmt;
        emitted = true;
        break;
      case DW_LNE_set_address:
        registers.address = {*elf, cursor.U64()};
        break;
      case DW_LNE_define_file: {
        auto compilationDir = table->Cu().Root()[DW_AT_comp_dir].AsString();
        auto file = ParseLineTableFile(cursor, compilationDir, table->includeDirectories);
        table->fileNames.push_back(file);
        break;
      }
      case DW_LNE_set_discriminator:
        registers.discriminator = cursor.Uleb128();
        break;
      default:
        Error::Send("Unexpected extended opcode");
    }
  } else {
    // special opcode
    auto adjustedOpcode = opcode - table->opcodeBase;
    registers.address += adjustedOpcode / table->lineRange;
    registers.line += table->lineBase + (adjustedOpcode % table->lineRange);
    current = registers;
    registers.basicBlockStart = false;
    registers.prologueEnd = false;
    registers.epilogueBegin = false;
    registers.discriminator = 0;
    emitted = true;
  }
  pos = cursor.Position();
  return emitted;
}

LineTable::iterator LineTable::GetEntryByAddress(FileAddr address) const {
  auto prev = begin();
  if (prev == end()) return prev;
  auto it = prev;
  for (++it; it != end(); prev = it++) {
    if (prev->address <= address && address < it->address && !prev->endSequence) return prev;
  }
  return end();
}

std::vector<LineTable::iterator> LineTable::GetEntriesByLine(std::filesystem::path path, std::size_t line) const {
  std::vector<iterator> entries;
  for (auto it = begin(); it != end(); ++it) {
    auto entryPath = it->fileEntry->path;
    if (it->line == line) {
      if ((path.is_absolute() && entryPath == path) || (path.is_relative() && PathEndsIn(entryPath, path))) {
        entries.push_back(it);
      }
    }
  }
  return entries;
}
}  // namespace ldb

const ldb::CallFrameInformation::CommonInformationEntry& ldb::CallFrameInformation::GetCIE(FileOffset offset) const {
  auto off = offset.Offset();
  if (cie_map_.contains(off)) {
    return cie_map_.at(off);
  }

  auto secton = offset.ElfFile()->GetSectionContents(".eh_frame");
  Cursor cursor{{offset.ElfFile()->FileOffsetAsDataPointer(offset), secton.End()}};

  auto cie = ParseCIE(cursor);

  auto [pos, _] = cie_map_.emplace(off, std::move(cie));
  return pos->second;
}

const std::byte* ldb::CallFrameInformation::EhHdr::operator[](FileAddr pc) const {
  auto elf = pc.ElfFile();
  auto text_section_start = *elf->GetSectonStartAddress(".text");
  // 一条表项含两列指针；列宽由 table_encoding 决定
  auto table_entry_encoding_size = EhFramePointerEncodingSize(encoding);
  auto row_size = table_entry_encoding_size * 2;
  // 表项格式：
  // ① initial_location（函数首 PC）
  // ② fde_ptr（FDE 在文件内的偏移）
  std::size_t low = 0;
  std::size_t high = count - 1;
  while (low <= high) {
    auto mid = (low + high) / 2;
    // mid 是表项索引，指向表项的开始位置
    Cursor cursor{{search_table + mid * row_size, search_table + count * row_size}};
    // initial_location的文件偏移
    auto current_offset = elf->DataPointerAsFileOffset(cursor.Position());
    auto eh_hdr_offset = elf->DataPointerAsFileOffset(start);
    // 解码initial_location
    auto entry_address = ParseEhFramePointer(*elf, cursor, encoding, current_offset.Offset(), text_section_start.Addr(),
                                             eh_hdr_offset.Offset(), 0);

    if (entry_address < pc.Addr()) {
      low = mid + 1;
    } else if (entry_address > pc.Addr()) {
      if (mid == 0) {
        ldb::Error::Send("Address not found in eh_hdr");
      }
      high = mid - 1;
    } else {
      high = mid;
      break;
    }
  }

  std::size_t row_index = high;
  const std::byte* row_ptr = search_table + row_index * row_size;
  // 第二列是fde_ptr
  Cursor fde_cur{{row_ptr + table_entry_encoding_size, row_ptr + row_size}};

  auto fde_offset = elf->DataPointerAsFileOffset(fde_cur.Position());
  auto hdr_offset = elf->DataPointerAsFileOffset(start);
  auto fde_offset_int = ParseEhFramePointer(*elf, fde_cur, encoding, fde_offset.Offset(), text_section_start.Addr(),
                                            hdr_offset.Offset(), 0);
  ldb::FileOffset fde_off{*elf, fde_offset_int};
  return elf->FileOffsetAsDataPointer(fde_off);
}