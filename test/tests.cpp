#include <sys/types.h>
#include <signal.h>

#include <catch2/catch_test_macros.hpp>

#include <libldb/process.hpp>

using namespace ldb;

namespace
{
	bool ProcessExists(pid_t pid)
	{
		auto ret = kill(pid, 0);
		return ret != -1 && errno != ESRCH;
	}
}

TEST_CASE("Process::Launch success", "[process]")
{
	auto proc = Process::Launch("yes");
	REQUIRE(ProcessExists(proc->Pid()));
}

#include <libldb/error.hpp>

TEST_CASE("Process::Launch no such program", "[process]")
{
	REQUIRE_THROWS_AS(Process::Launch("you_do_not_have_to_be_good"), Error);
}