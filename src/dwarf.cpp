#include <algorithm>
#include <libldb/bit.hpp>
#include <libldb/dwarf.hpp>
#include <libldb/elf.hpp>
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
}  // namespace

const std::unordered_map<std::uint64_t, ldb::Abbrev>&
ldb::Dwarf::GetAbbrevTable(std::size_t offset) {
  if (!abbrev_tables_.contains(offset)) {
    abbrev_tables_.emplace(offset, ParseAbbrevTable(*elf_, offset));
  }
  return abbrev_tables_.at(offset);
}