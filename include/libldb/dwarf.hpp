#pragma once

#include <libldb/detail/dwarf.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <libldb/types.hpp>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace ldb {

struct AttrSpec {
  std::uint64_t attr;
  std::uint64_t form;
};

struct Abbrev {
  std::uint64_t code;
  std::uint64_t tag;
  bool hasChildren;
  std::vector<AttrSpec> attrSpecs;
};

class Die;
class CompileUnit;
class RangeList {
 public:
  RangeList(const CompileUnit* _compileUnit, Span<const std::byte> _data, FileAddr _baseAddr)
      : compileUnit(_compileUnit), data(_data), baseAddr(_baseAddr) {}

  struct Entry {
    FileAddr low, high;

    bool Contains(FileAddr addr) const { return low <= addr && addr < high; }
  };

  class iterator;
  iterator begin() const;
  iterator end() const;

  bool Contains(FileAddr addr) const;

 private:
  const CompileUnit* compileUnit;
  Span<const std::byte> data;
  FileAddr baseAddr;
};

class RangeList::iterator {
 public:
  using value_type = Entry;
  using reference = const Entry&;
  using pointer = const Entry*;
  using difference_type = std::ptrdiff_t;
  using iterator_category = std::forward_iterator_tag;

  iterator(const CompileUnit* _compileUnit, Span<const std::byte> _data, FileAddr _baseAddress);

  iterator() = default;
  iterator(const iterator&) = default;
  iterator& operator=(const iterator&) = default;

  explicit iterator(const Die& _die);

  const Entry& operator*() const { return current; }
  const Entry* operator->() const { return &current; }

  bool operator==(iterator rhs) const { return pos == rhs.pos; }
  bool operator!=(iterator rhs) const { return pos != rhs.pos; }

  iterator& operator++();
  iterator operator++(int);

 private:
  const CompileUnit* compileUnit;
  Span<const std::byte> data{};
  FileAddr baseAddress;
  const std::byte* pos{};
  Entry current;
};

class CompileUnit;
class Die;
class Attr {
 public:
  Attr(const CompileUnit* _compileUnit, std::uint64_t _type, std::uint64_t _form, const std::byte* _location)
      : compileUnit(_compileUnit), type(_type), form(_form), location(_location) {}

  std::uint64_t Name() const { return type; }
  std::uint64_t Form() const { return form; }

  FileAddr AsAddress() const;

  std::uint32_t AsSectionOffset() const;

  Span<const std::byte> AsBlock() const;

  std::uint64_t AsInt() const;

  std::string_view AsString() const;

  Die AsReference() const;

  RangeList AsRangeList() const;

 private:
  const CompileUnit* compileUnit;
  std::uint64_t type;
  std::uint64_t form;
  const std::byte* location;
};

class CompileUnit;
class LineTable {
 public:
  struct Entry;

  struct File {
    std::filesystem::path path;
    std::uint64_t modificationTime;
    std::uint64_t fileLength;
  };

  LineTable(Span<const std::byte> _data, const CompileUnit* _compileUnit, bool _defaultIsStmt, std::int8_t _lineBase,
            std::uint8_t _lineRange, std::uint8_t _opcodeBase, std::vector<std::filesystem::path> _includeDirectories,
            std::vector<File> _fileNames)
      : data(_data),
        compileUnit(_compileUnit),
        defaultIsStmt(_defaultIsStmt),
        lineBase(_lineBase),
        lineRange(_lineRange),
        opcodeBase(_opcodeBase),
        includeDirectories(std::move(_includeDirectories)),
        fileNames(std::move(_fileNames)) {}
  LineTable(const LineTable&) = delete;
  LineTable& operator=(const LineTable&) = delete;
  LineTable(LineTable&&) = delete;
  LineTable& operator=(LineTable&&) = delete;
  ~LineTable() = default;

  const CompileUnit& Cu() const { return *compileUnit; }

  const std::vector<File>& FileNames() const { return fileNames; }

  class iterator;

  iterator begin() const;
  iterator end() const;

  iterator GetEntryByAddress(FileAddr address) const;
  std::vector<iterator> GetEntriesByLine(std::filesystem::path path, std::size_t line) const;

 private:
  Span<const std::byte> data;
  const CompileUnit* compileUnit;
  bool defaultIsStmt;
  std::int8_t lineBase;
  std::uint8_t lineRange;
  std::uint8_t opcodeBase;
  std::vector<std::filesystem::path> includeDirectories;
  mutable std::vector<File> fileNames;
};

struct LineTable::Entry {
  FileAddr address;
  std::uint64_t fileIndex = 1;
  std::uint64_t line = 1;
  std::uint64_t column = 0;
  bool isStmt;
  bool basicBlockStart = false;
  bool endSequence = false;
  bool prologueEnd = false;
  bool epilogueBegin = false;
  std::uint64_t discriminator = 0;
  File* fileEntry = nullptr;

  bool operator==(const Entry& rhs) const {
    return address == rhs.address && fileIndex == rhs.fileIndex && line == rhs.line && column == rhs.column &&
           discriminator == rhs.discriminator;
  }
};

class LineTable::iterator {
 public:
  using value_type = Entry;
  using reference = const Entry&;
  using pointer = const Entry*;
  using difference_type = std::ptrdiff_t;
  using iterator_category = std::forward_iterator_tag;

  iterator() = default;
  iterator(const iterator&) = default;
  iterator& operator=(const iterator&) = default;

  explicit iterator(const LineTable* table);

  const LineTable::Entry& operator*() const { return current; }
  const LineTable::Entry* operator->() const { return &current; }

  iterator& operator++();
  iterator operator++(int);

  bool operator==(const iterator& rhs) const { return pos == rhs.pos; }
  bool operator!=(const iterator& rhs) const { return !(*this == rhs); }

 private:
  bool ExecuteInstruction();

  const LineTable* table;
  LineTable::Entry current;
  LineTable::Entry registers;
  const std::byte* pos;
};

class Die;
class Dwarf;
class CompileUnit {
 public:
  CompileUnit(Dwarf& _parent, Span<const std::byte> _data, std::size_t _abbrevOffset);

  const Dwarf* DwarfInfo() const { return parent; }

  Span<const std::byte> Data() const { return data; }

  const std::unordered_map<std::uint64_t, Abbrev>& AbbrevTable() const;

  Die Root() const;

  const LineTable& Lines() const { return *lineTable; }

 private:
  Dwarf* parent;
  Span<const std::byte> data;
  std::size_t abbrevOffset;
  std::unique_ptr<LineTable> lineTable;
};

class Dwarf;
class CallFrameInformation {
 public:
  /// 为什么 CIE 必不可少？
  /// 1. 节省空间：许多函数共有的“返回地址寄存器”“指针编码”“默认 CFA 规则”等信息写一次即可。
  /// 2. 提供默认上下文：解释 FDE 指令时，需要先把 CIE 的 initial_instructions
  /// 执行一遍，建立“到达函数开头时”寄存器/栈的初始状态，然后 FDE 再在此基础上追加/修改规则。
  /// 3. 确定指针编码 (R)：FDE 里的 initial_location / address_range 若没有 CIE
  /// 指明编码，解析器就无法正确解析绝对/相对地址。

  /// 存放多条 FDE 共同的部分
  struct CommonInformationEntry {
    /// 当前 CIE 条目总长，不含自身；若为 0xffffffff 则使用 64 bit 扩展长度（随后跟 8 B length64）。
    std::uint32_t length;
    /// CFI 指令里求 CFA 时乘用,用在 DW_CFA_advance_loc：指令偏移 × CAF = 字节数。
    std::uint64_t code_alignment_factor;
    /// 同上，用于偏移带符号伸缩,调整栈向上/向下方向；x86-64 上通常 = -8。
    std::int64_t data_alignment_factor;
    /// 标记后续 FDE 是否有增补区
    bool fde_has_augmentation;
    /// 'R'指定的编码方式
    /// 如果 fde_has_augmentation 为真，说明 CIE 的 augmentation string 有 R字符，需要记录 FDE 中 code-pointer
    /// 的编码方式（比如 DW_EH_PE_udata4 / DW_EH_PE_pcrel 等）, 解析 initial_location / address_range 时用。
    std::uint8_t fde_pointer_encoding;
    /// CIE 本身的 CFI 指令序列, 解释器需要把它作为“初始状态”
    Span<const std::byte> instructions;
  };

  /// 存放单条 FDE 的条目，每个函数对应一个 FDE
  struct FrameDescriptionEntry {
    /// FDE 总长，含自身
    std::uint32_t length;
    /// 所属CIE
    const CommonInformationEntry* cie;
    /// 函数起始地址：解析时要先按 CIE 的 R 编码转换成绝对地址
    FileAddr initial_location;
    /// 覆盖的字节区间大小：[initial_location , initial_location + address_range)
    std::uint64_t address_range;
    /// FDE 独有的一串 CFI 指令 (DW_CFA_*)，用于从 CIE 初始状态继续补充 / 覆盖，得到该函数每个 PC 区间的 CFA &
    /// 寄存器恢复规则。
    Span<const std::byte> instructions;
  };

  /// 加速索引表，让运行时/调试器能 O(log N) 从 任意 PC 迅速找到对应 FDE。内容：
  ///  * 版本 & 3 个编码字节
  ///  * 指向 .eh_frame 的指针
  ///  * FDE 数量
  ///  * 二分表：(initial_location , fde_ptr) × N
  ///      每条连续存放两列：
  ///      ① initial_location（函数首 PC）
  ///      ② fde_ptr（FDE 在文件内的偏移）
  ///     两列都用 table_encoding 编码。表按 initial_location 递增排序，可用二分。
  struct EhHdr {
    /// .eh_frame_hdr节的起始地址
    const std::byte* start;
    /// 表的开始位置
    const std::byte* search_table;
    /// 搜索表中条目数:FDE 的个数
    std::size_t count;
    /// 表项编码
    std::uint8_t encoding;
    CallFrameInformation* parent;

    const std::byte* operator[](FileAddr pc) const;
  };

  CallFrameInformation() = delete;
  CallFrameInformation(const CallFrameInformation&) = delete;
  CallFrameInformation& operator=(const CallFrameInformation&) = delete;
  CallFrameInformation(CallFrameInformation&&) = delete;
  CallFrameInformation& operator=(CallFrameInformation&&) = delete;

  const Dwarf& DwarfInfo() const { return *dwarf_; }

  /// 获取 CIE 条目, 键为 CIE 的 file-offset
  const CommonInformationEntry& GetCIE(FileOffset offset) const;

 private:
  const Dwarf* dwarf_ = nullptr;

  /// 保存所有 CIE 条目, 键为 CIE 的 offset
  mutable std::unordered_map<std::uint64_t, CommonInformationEntry> cie_map_;
};

class Elf;
class Dwarf {
 public:
  Dwarf(const Elf& parent);

  const Elf* ElfFile() const { return elf; }

  const std::unordered_map<std::uint64_t, Abbrev>& GetAbbrevTable(std::size_t offset);

  const std::vector<std::unique_ptr<CompileUnit>>& CompileUnits() const { return compileUnits; }

  const CompileUnit* CompileUnitContainingAddress(FileAddr address) const;

  std::optional<Die> FunctionContainingAddress(FileAddr address) const;

  std::vector<Die> FindFunctions(std::string name) const;

  LineTable::iterator LineEntryAtAddress(FileAddr address) {
    auto compileUnit = CompileUnitContainingAddress(address);
    if (!compileUnit) return {};
    return compileUnit->Lines().GetEntryByAddress(address);
  }

  std::vector<Die> InlineStackAtAddress(FileAddr address) const;

 private:
  void Index() const;
  void IndexDie(const Die& current) const;

  struct IndexEntry {
    const CompileUnit* compileUnit;
    const std::byte* pos;
  };

  const Elf* elf;

  std::unordered_map<std::size_t, std::unordered_map<std::uint64_t, Abbrev>> abbrevTables;

  std::vector<std::unique_ptr<CompileUnit>> compileUnits;

  mutable std::unordered_multimap<std::string, IndexEntry> functionIndex;
};

// for inline func
struct SourceLoaction {
  const LineTable::File* file;
  std::uint64_t line;
};

class Die {
 public:
  explicit Die(const std::byte* _next) : next(_next) {}

  Die(const std::byte* _position, const CompileUnit* _compileUnit, const Abbrev* _abbrev,
      std::vector<const std::byte*> _attrLocations, const std::byte* _next)
      : position(_position),
        compileUnit(_compileUnit),
        abbrev(_abbrev),
        attrLocations(std::move(_attrLocations)),
        next(_next) {}

  const CompileUnit* Cu() const { return compileUnit; }

  const Abbrev* AbbrevEntry() const { return abbrev; }

  const std::byte* Position() const { return position; }

  const std::byte* Next() const { return next; }

  std::optional<std::string_view> Name() const;

  // the attribute must different in a same DIE
  bool Contains(std::uint64_t attribute) const;

  // get the value of the attribute
  Attr operator[](std::uint64_t attribute) const;

  FileAddr LowPc() const;
  FileAddr HighPc() const;

  bool ContainsAddress(FileAddr address) const;

  SourceLoaction Location() const;

  const LineTable::File& File() const;

  std::uint64_t Line() const;

  class ChildrenRange;
  ChildrenRange Children() const;

 private:
  const std::byte* position = nullptr;
  const CompileUnit* compileUnit = nullptr;
  const Abbrev* abbrev = nullptr;
  const std::byte* next = nullptr;
  std::vector<const std::byte*> attrLocations;
};

class Die::ChildrenRange {
 public:
  ChildrenRange(Die _die) : die(std::move(_die)) {}

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

    explicit iterator(const Die& _die);

    const Die& operator*() const { return *die; }
    const Die* operator->() const { return &die.value(); }

    iterator& operator++();
    iterator operator++(int);

    bool operator==(const iterator& rhs) const;
    bool operator!=(const iterator& rhs) const { return !(*this == rhs); }

   private:
    std::optional<Die> die;
  };

  iterator begin() const {
    if (die.abbrev->hasChildren) {
      return iterator{die};
    }
    return end();
  }

  iterator end() const { return {}; }

 private:
  Die die;
};
}  // namespace ldb