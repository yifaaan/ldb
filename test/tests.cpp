#include <catch2/catch_test_macros.hpp>
#include <cerrno>
#include <csignal>
#include <fstream>
#include <libldb/bit.hpp>
#include <libldb/error.hpp>
#include <libldb/pipe.hpp>
#include <libldb/process.hpp>

#include "libldb/register_info.hpp"

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