#include <elf.h>

#include <catch2/catch_test_macros.hpp>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <fstream>
#include <libldb/bit.hpp>
#include <libldb/error.hpp>
#include <libldb/pipe.hpp>
#include <libldb/process.hpp>
#include <libldb/register_info.hpp>
#include <libldb/types.hpp>
#include <regex>

using namespace ldb;
namespace {
// Check if the process exists.
bool ProcessExists(pid_t pid) {
  // ret = -1 means kill failed.
  // errno = ESRCH means the process does not exist.
  auto ret = kill(pid, 0);
  return ret != -1 && errno != ESRCH;
}

// Get the state of the process from /proc/<pid>/stat.
char GetProcessState(pid_t pid) {
  std::ifstream stat_file{"/proc/" + std::to_string(pid) + "/stat"};
  std::string line;
  std::getline(stat_file, line);
  auto index_of_first_right_parenthesis = line.rfind(')');
  auto index_of_status_indicator = index_of_first_right_parenthesis + 2;
  return line[index_of_status_indicator];
}

// 获取Section加载偏移量
// 当file_address和file_offset不相同时，需要file_address -
// file_offset得到偏移量， file_address: 开启PIE时相对于运行时加载地址的偏移量

std::int64_t GetSectionLoadBias(std::filesystem::path path,
                                Elf64_Addr file_address) {
  auto command = std::string{"readelf -WS "} + path.string();
  auto pipe = popen(command.c_str(), "r");

  std::regex text_regex(R"(PROGBITS\s+(\w+)\s+(\w+)\s+(\w+))");
  char* line = nullptr;
  std::size_t len = 0;
  while (getline(&line, &len, pipe) != -1) {
    std::cmatch groups;
    if (std::regex_search(line, groups, text_regex)) {
      auto section_file_address = std::stol(groups[1], nullptr, 16);
      // 在elf文件中的偏移量
      auto section_file_offset = std::stol(groups[2], nullptr, 16);
      // Section的大小
      auto size = std::stol(groups[3], nullptr, 16);
      // 如果文件地址在这个段内
      if (section_file_address <= file_address &&
          file_address < (section_file_address + size)) {
        free(line);
        pclose(pipe);
        return section_file_address - section_file_offset;
      }
    }
    free(line);
    line = nullptr;
  }
  pclose(pipe);
  ldb::Error::Send("Could not find seciton load bias");
}

/*
文件布局                    内存布局
+----------------+         +----------------+
| ...            |         | ...            |
| Offset 0x450  | ------> | 0x400450       | (.text)
| ...            |         | ...            |
| Offset 0x1000  | ------> | 0x601000       | (.data)
+----------------+         +----------------+
*/

// 获取入口点的文件偏移量
std::int64_t GetEntryPointOffset(std::filesystem::path path) {
  std::ifstream elf_file{path};

  Elf64_Ehdr header;
  elf_file.read(reinterpret_cast<char*>(&header), sizeof(header));

  // 开启PIE时，e_entry是相对于运行时加载地址的偏移量(file address)
  auto entry_file_address = header.e_entry;
  // 获取入口点在elf文件中的偏移量:file_address - 其所属section的加载偏移
  return entry_file_address - GetSectionLoadBias(path, entry_file_address);
}

// offset是入口点在elf文件中的偏移量
VirtAddr GetLoadAddress(pid_t pid, std::int64_t offset) {
  std::ifstream maps{"/proc/" + std::to_string(pid) + "/maps"};
  std::regex map_regex(R"((\w+)-\w+ ..(.). (\w+))");

  std::string data;
  while (std::getline(maps, data)) {
    std::smatch groups;
    std::regex_search(data, groups, map_regex);
    if (groups[2] == 'x') {
      // 该段的虚拟加载基地址
      auto low_range = std::stol(groups[1], nullptr, 16);
      // 该段在文件中的偏移量
      auto file_offset = std::stol(groups[3], nullptr, 16);
      return VirtAddr{static_cast<uint64_t>(offset - file_offset + low_range)};
    }
  }
  ldb::Error::Send("Could not find load address");
}
}  // namespace

TEST_CASE("Process::Launch success", "[process]") {
  auto process = Process::Launch("yes");
  REQUIRE(ProcessExists(process->pid()));
}

TEST_CASE("Process::Launch failed", "[process]") {
  REQUIRE_THROWS_AS(Process::Launch("nonexistent"), Error);
}

TEST_CASE("Process::Attach success", "[process]") {
  // Launch a process that will run endlessly. And debug is false.
  auto target = Process::Launch("test/targets/run_endlessly", false);
  auto process = Process::Attach(target->pid());
  REQUIRE(GetProcessState(target->pid()) == 't');
}

TEST_CASE("Process::Attach invalid PID", "[process]") {
  REQUIRE_THROWS_AS(Process::Attach(0), Error);
}

TEST_CASE("Process::Resume success", "[process]") {
  {
    auto process = Process::Launch("test/targets/run_endlessly");
    process->Resume();
    auto status = GetProcessState(process->pid());
    auto success = status == 'R' || status == 'S';
    REQUIRE(success);
  }

  {
    auto target = Process::Launch("test/targets/run_endlessly", false);
    auto process = Process::Attach(target->pid());
    process->Resume();
    auto status = GetProcessState(process->pid());
    auto success = status == 'R' || status == 'S';
    REQUIRE(success);
  }
}

TEST_CASE("Process::Resume already terminated", "[process]") {
  auto process = Process::Launch("test/targets/end_immediately");
  process->Resume();
  process->WaitOnSignal();
  REQUIRE_THROWS_AS(process->Resume(), Error);
}

TEST_CASE("Write register works", "[register]") {
  bool close_on_exec = false;
  ldb::Pipe channel{close_on_exec};

  auto process =
      Process::Launch("test/targets/reg_write", true, channel.GetWrite());
  channel.CloseWrite();

  process->Resume();
  // reg_write process traps itself.
  process->WaitOnSignal();

  // Write the rsi register(the second parameter of printf) so that the
  // reg_write process prints 0xcafecafe.
  auto& regs = process->registers();
  regs.WriteById(RegisterId::rsi, 0xcafecafe);
  // Resume the reg_write process to call printf.
  process->Resume();
  process->WaitOnSignal();

  auto output = channel.Read();
  REQUIRE(ToStringView(output) == "0xcafecafe");
  // Then reg_write process traps because we need to write mm0.

  regs.WriteById(RegisterId::mm0, 0xba5eba11);
  process->Resume();
  process->WaitOnSignal();

  output = channel.Read();
  REQUIRE(ToStringView(output) == "0xba5eba11");

  regs.WriteById(RegisterId::xmm0, 42.24);
  process->Resume();
  process->WaitOnSignal();
  output = channel.Read();
  REQUIRE(ToStringView(output) == "42.24");

  regs.WriteById(RegisterId::st0, 42.24l);
  // 设置状态字(fsw)以指示FPU栈的状态
  // 位11-13(0b111)指示栈顶位置为7，这意味着下一个压栈操作将使用st0
  regs.WriteById(RegisterId::fsw, std::uint16_t{0b0011100000000000});
  // 设置标签字(ftw)以指示哪些寄存器有效
  // 对于st0设置为0b00(有效)，其他寄存器设置为0b11(空)
  regs.WriteById(RegisterId::ftw, std::uint16_t{0b0011111111111111});
  process->Resume();
  process->WaitOnSignal();
  output = channel.Read();
  REQUIRE(ToStringView(output) == "42.24");
}

TEST_CASE("Read register works", "[register]") {
  auto process = Process::Launch("test/targets/reg_read");
  auto& regs = process->registers();
  process->Resume();
  // reg_read process traps itself at reg_read.s:36 for now.
  process->WaitOnSignal();

  REQUIRE(regs.ReadByIdAs<std::uint64_t>(RegisterId::r13) == 0xcafecafe);

  process->Resume();
  // reg_read process traps itself at reg_read.s:40 for now.
  process->WaitOnSignal();
  REQUIRE(regs.ReadByIdAs<std::uint8_t>(RegisterId::r13b) == 42);

  process->Resume();
  // reg_read process traps itself at reg_read.s:46 for now.
  process->WaitOnSignal();
  REQUIRE(regs.ReadByIdAs<Byte64>(RegisterId::mm0) == ToByte64(0xba5eba11ull));

  process->Resume();
  // reg_read process traps itself at reg_read.s:51 for now.
  process->WaitOnSignal();
  REQUIRE(regs.ReadByIdAs<Byte128>(RegisterId::xmm0) == ToByte128(64.125));

  process->Resume();
  // reg_read process traps itself at reg_read.s:57 for now.
  process->WaitOnSignal();
  REQUIRE(regs.ReadByIdAs<long double>(RegisterId::st0) == 64.125l);
}

TEST_CASE("Can create breakpoint site", "[breakpoint]") {
  auto process = Process::Launch("test/targets/run_endlessly");
  auto& site = process->CreateBreakpointSite(VirtAddr{42});
  REQUIRE(site.address().addr() == 42);
}

TEST_CASE("Breakpoint site ids increase", "[breakpoint]") {
  auto process = Process::Launch("test/targets/run_endlessly");
  auto& s1 = process->CreateBreakpointSite(VirtAddr{42});
  auto& s2 = process->CreateBreakpointSite(VirtAddr{43});
  REQUIRE(s2.id() == s1.id() + 1);

  auto& s3 = process->CreateBreakpointSite(VirtAddr{44});
  REQUIRE(s3.id() == s2.id() + 1);

  auto& s4 = process->CreateBreakpointSite(VirtAddr{45});
  REQUIRE(s4.id() == s3.id() + 1);
}

TEST_CASE("Can find breakpoint site", "[breakpoint]") {
  auto process = Process::Launch("test/targets/run_endlessly");
  const auto& cprocess = *process;

  process->CreateBreakpointSite(VirtAddr{42});
  process->CreateBreakpointSite(VirtAddr{43});
  process->CreateBreakpointSite(VirtAddr{44});
  process->CreateBreakpointSite(VirtAddr{45});

  auto& s1 = process->breakpoint_sites().GetByAddress(VirtAddr{44});
  REQUIRE(process->breakpoint_sites().ContainsAddress(VirtAddr{44}));
  REQUIRE(s1.address().addr() == 44);

  auto& cs1 = cprocess.breakpoint_sites().GetByAddress(VirtAddr{44});
  REQUIRE(cprocess.breakpoint_sites().ContainsAddress(VirtAddr{44}));
  REQUIRE(cs1.address().addr() == 44);

  auto& s2 = process->breakpoint_sites().GetById(s1.id() + 1);
  REQUIRE(process->breakpoint_sites().ContainsId(s1.id() + 1));
  REQUIRE(s2.id() == s1.id() + 1);
  REQUIRE(s2.address().addr() == 45);

  auto& cs2 = cprocess.breakpoint_sites().GetById(cs1.id() + 1);
  REQUIRE(cprocess.breakpoint_sites().ContainsId(cs1.id() + 1));
  REQUIRE(cs2.address().addr() == 45);
}

TEST_CASE("Cannot find breakpoint site", "[breakpoint]") {
  auto process = Process::Launch("test/targets/run_endlessly");
  const auto& cprocess = *process;

  REQUIRE_THROWS_AS(process->breakpoint_sites().GetByAddress(VirtAddr{44}),
                    Error);
  REQUIRE_THROWS_AS(process->breakpoint_sites().GetById(1), Error);

  REQUIRE_THROWS_AS(cprocess.breakpoint_sites().GetByAddress(VirtAddr{44}),
                    Error);
  REQUIRE_THROWS_AS(cprocess.breakpoint_sites().GetById(1), Error);
}

TEST_CASE("Breakpoint site list size and emptiness", "[breakpoint]") {
  auto process = Process::Launch("test/targets/run_endlessly");
  const auto& cprocess = *process;

  REQUIRE(process->breakpoint_sites().Empty());
  REQUIRE(cprocess.breakpoint_sites().Empty());

  process->CreateBreakpointSite(VirtAddr{42});
  process->CreateBreakpointSite(VirtAddr{43});
  REQUIRE(process->breakpoint_sites().Size() == 2);
  REQUIRE(cprocess.breakpoint_sites().Size() == 2);
  REQUIRE(!process->breakpoint_sites().Empty());
  REQUIRE(!cprocess.breakpoint_sites().Empty());

  process->CreateBreakpointSite(VirtAddr{44});
  process->CreateBreakpointSite(VirtAddr{45});

  REQUIRE(process->breakpoint_sites().Size() == 4);
  REQUIRE(cprocess.breakpoint_sites().Size() == 4);
}

TEST_CASE("Can iterate over breakpoint sites", "[breakpoint]") {
  auto process = Process::Launch("test/targets/run_endlessly");
  const auto& cprocess = *process;

  process->CreateBreakpointSite(VirtAddr{42});
  process->CreateBreakpointSite(VirtAddr{43});
  process->CreateBreakpointSite(VirtAddr{44});
  process->CreateBreakpointSite(VirtAddr{45});

  process->breakpoint_sites().ForEach([addr = 42](const auto& site) mutable {
    REQUIRE(site.address().addr() == addr++);
  });

  cprocess.breakpoint_sites().ForEach([addr = 42](const auto& site) mutable {
    REQUIRE(site.address().addr() == addr++);
  });
}

TEST_CASE("Breakpoint on address works", "[breakpoint]") {
  bool close_on_exec = false;
  ldb::Pipe channel{close_on_exec};

  auto process =
      Process::Launch("test/targets/hello_ldb", true, channel.GetWrite());
  channel.CloseWrite();

  auto offset = GetEntryPointOffset("test/targets/hello_ldb");
  auto load_address = GetLoadAddress(process->pid(), offset);

  process->CreateBreakpointSite(load_address).Enable();
  process->Resume();
  auto reason = process->WaitOnSignal();

  REQUIRE(reason.reason == ProcessState::Stopped);
  REQUIRE(reason.info == SIGTRAP);
  REQUIRE(process->GetPc() == load_address);
  process->Resume();
  reason = process->WaitOnSignal();

  REQUIRE(reason.reason == ProcessState::Exited);
  REQUIRE(reason.info == 0);

  auto data = channel.Read();
  REQUIRE(ToStringView(data) == "Hello, LDB!\n");
}

TEST_CASE("Can remove breakpoint sites", "[breakpoint]") {
  auto process = Process::Launch("test/targets/run_endlessly");

  auto& site = process->CreateBreakpointSite(VirtAddr{42});
  process->CreateBreakpointSite(VirtAddr{43});
  REQUIRE(process->breakpoint_sites().Size() == 2);
  process->breakpoint_sites().RemoveById(site.id());
  process->breakpoint_sites().RemoveByAddress(VirtAddr{43});
  REQUIRE(process->breakpoint_sites().Empty());
}

TEST_CASE("Reading and writing memory works", "[memory]") {
  bool close_on_exec = false;
  Pipe channel{close_on_exec};

  auto process =
      Process::Launch("test/targets/memory", true, channel.GetWrite());
  channel.CloseWrite();
  process->Resume();
  process->WaitOnSignal();

  auto a_pointer = FromBytes<std::uint64_t>(channel.Read().data());
  auto data_vec =
      process->ReadMemory(VirtAddr{a_pointer}, sizeof(unsigned long long));
  auto data = FromBytes<std::uint64_t>(data_vec.data());
  REQUIRE(data == 0xcafecafe);

  process->Resume();
  process->WaitOnSignal();

  auto b_pointer = FromBytes<std::uint64_t>(channel.Read().data());
  process->WriteMemory(VirtAddr{b_pointer}, {AsBytes("Hello, ldb!"), 12});
  process->Resume();
  process->WaitOnSignal();

  auto read = channel.Read();
  REQUIRE(ToStringView(read) == "Hello, ldb!");
}

TEST_CASE("Hardware breakpoint evades memory checksums", "[breakpoint]") {
  bool close_on_exec = false;
  Pipe channel{close_on_exec};
  auto process =
      Process::Launch("test/targets/anti_debugger", true, channel.GetWrite());
  channel.CloseWrite();
  process->Resume();
  process->WaitOnSignal();

  auto func = VirtAddr{FromBytes<std::uint64_t>(channel.Read().data())};

  auto& soft = process->CreateBreakpointSite(func, false);
  // Change the instruction text at the breakpoint site.
  soft.Enable();

  process->Resume();
  process->WaitOnSignal();
  REQUIRE(ToStringView(channel.Read()) == "Putting pepperni on pizza...\n");

  process->breakpoint_sites().RemoveById(soft.id());

  auto& hard = process->CreateBreakpointSite(func, true);
  hard.Enable();

  process->Resume();
  process->WaitOnSignal();
  REQUIRE(process->GetPc() == func);

  process->Resume();
  REQUIRE(ToStringView(channel.Read()) == "Putting pineapple on pizza...\n");
}

TEST_CASE("Watchpoint detects read", "[watchpoint]") {
  bool close_on_exec = false;
  Pipe channel{close_on_exec};
  auto process =
      Process::Launch("test/targets/anti_debugger", true, channel.GetWrite());
  channel.CloseWrite();
  process->Resume();
  // STGTRAP on raise(SIGTRAP):24
  process->WaitOnSignal();
  // Read the address of AnInnocentFunction
  auto func = VirtAddr{FromBytes<std::uint64_t>(channel.Read().data())};

  auto& watch = process->CreateWatchpoint(func, StoppointMode::ReadWrite, 1);
  watch.Enable();
  process->Resume();
  // TRAP on func
  process->WaitOnSignal();

  process->StepInstruction();
  auto& soft = process->CreateBreakpointSite(func, false);
  soft.Enable();

  process->Resume();
  // TRAP at raise(SIGTRAP):33
  auto reason = process->WaitOnSignal();
  REQUIRE(reason.info == SIGTRAP);

  process->Resume();
  // TRAP at func
  process->WaitOnSignal();

  REQUIRE(ToStringView(channel.Read()) == "Putting pineapple on pizza...\n");

  process->Resume();
  // TRAP at func
  process->WaitOnSignal();

  REQUIRE(ToStringView(channel.Read()) == "Putting pineapple on pizza...\n");
}