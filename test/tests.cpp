#include <catch2/catch_test_macros.hpp>
#include <csignal>
#include <libldb/error.hpp>
#include <libldb/process.hpp>

using namespace ldb;

namespace {
/// Check if a process exists.
bool process_exists(pid_t pid) {
  auto ret = kill(pid, 0);
  return ret != -1 && errno != ESRCH;
}
} // namespace

TEST_CASE("process::launch success", "[process]") {
  auto proc = process::launch("yes");
  REQUIRE(process_exists(proc->pid()));
}

TEST_CASE("process::launch no such program", "[process]") {
  REQUIRE_THROWS_AS(process::launch("you_do_not_have_to_be_good"), error);
}