#include <catch2/catch_test_macros.hpp>
#include <cerrno>
#include <csignal>
#include <fstream>
#include <libldb/process.hpp>

#include "libldb/error.hpp"

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