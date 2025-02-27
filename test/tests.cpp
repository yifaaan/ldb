#include <catch2/catch_test_macros.hpp>
#include <cerrno>
#include <csignal>
#include <libldb/process.hpp>

#include "libldb/error.hpp"

using namespace ldb;
namespace {
// Check if the process exists.
bool process_exists(pid_t pid) {
  // ret = -1 means kill failed.
  // errno = ESRCH means the process does not exist.
  auto ret = kill(pid, 0);
  return ret != -1 && errno != ESRCH;
}
}  // namespace

TEST_CASE("Process::Launch success", "[process]") {
  auto process = Process::Launch("yes");
  REQUIRE(process_exists(process->Pid()));
}

TEST_CASE("Process::Launch failed", "[process]") {
  REQUIRE_THROWS_AS(Process::Launch("nonexistent"), Error);
}