#include <catch2/catch_test_macros.hpp>
#include <csignal>
#include <fstream>
#include <libldb/bit.hpp>
#include <libldb/error.hpp>
#include <libldb/pipe.hpp>
#include <libldb/process.hpp>

using namespace ldb;

namespace {
/// Check if a process exists.
bool process_exists(pid_t pid) {
  auto ret = kill(pid, 0);
  return ret != -1 && errno != ESRCH;
}

char get_process_status(pid_t pid) {
  auto stat_path = std::string("/proc/") + std::to_string(pid) + "/stat";
  std::fstream stat{stat_path};
  std::string data;
  std::getline(stat, data);
  auto index_of_last_parenthesis = data.rfind(')');
  auto index_of_status_indicator = index_of_last_parenthesis + 2;
  return data[index_of_status_indicator];
}
}  // namespace

TEST_CASE("process::launch success", "[process]") {
  auto proc = process::launch("yes");
  REQUIRE(process_exists(proc->pid()));
}

TEST_CASE("process::launch no such program", "[process]") {
  REQUIRE_THROWS_AS(process::launch("you_do_not_have_to_be_good"), error);
}

TEST_CASE("process::attach success", "[process]") {
  auto target = process::launch(
      "/root/workspace/ldb/build/test/targets/run_endlessly", false);
  auto proc = process::attach(target->pid());
  REQUIRE(get_process_status(target->pid()) == 't');
}

TEST_CASE("process::attach invalid PID", "[process]") {
  REQUIRE_THROWS_AS(process::attach(0), error);
}

TEST_CASE("process::resume success", "[process]") {
  {
    auto proc =
        process::launch("/root/workspace/ldb/build/test/targets/run_endlessly");
    proc->resume();
    auto status = get_process_status(proc->pid());
    auto success = status == 'R' or status == 'S';
    REQUIRE(success);
  }
  {
    auto target =
        process::launch("/root/workspace/ldb/build/test/targets/run_endlessly");
    auto proc = process::attach(target->pid());
    proc->resume();
    auto status = get_process_status(proc->pid());
    auto success = status == 'R' or status == 'S';
    REQUIRE(!success);
    // INFO("msg");
  }
}

TEST_CASE("process::resume already terminated", "[process]") {
  auto proc =
      process::launch("/root/workspace/ldb/build/test/targets/end_immediately");
  INFO(proc->pid());
  proc->resume();
  proc->wait_on_signal();
  REQUIRE_THROWS_AS(proc->resume(), error);
}

TEST_CASE("Write register works", "[register]") {
  bool close_on_exec = false;
  ldb::pipe channel(close_on_exec);

  auto proc =
      process::launch("/root/workspace/ldb/build/test/targets/reg_write", true,
                      channel.get_write());
  channel.close_write();

  proc->resume();
  proc->wait_on_signal();

  auto& regs = proc->get_registers();
  regs.write_by_id(register_id::rsi, 0xabcdabcd);

  proc->resume();
  proc->wait_on_signal();

  auto output = channel.read();
  REQUIRE(to_string_view(output) == "0xabcdabcd");
}