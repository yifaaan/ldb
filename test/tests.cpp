#include <sys/types.h>
#include <signal.h>

#include <fstream>
#include <format>

#include <catch2/catch_test_macros.hpp>

#include <libldb/process.hpp>
#include <libldb/error.hpp>

using namespace ldb;

namespace
{
	/// <summary>
	/// check if the process with the given pid exists
	/// </summary>
	/// <param name="pid"></param>
	/// <returns>whether the process exists or not</returns>
	bool ProcessExists(pid_t pid)
	{
		auto ret = kill(pid, 0);
		return ret != -1 && errno != ESRCH;
	}

	/// <summary>
	/// get the status of the process with the given pid by reading /proc/[pid]/stat file
	/// </summary>
	/// <param name="pid"></param>
	/// <returns>process status</returns>
	char GetProcessStatus(pid_t pid)
	{
		std::ifstream stat{ std::format("/proc/{}/stat", pid) };
		std::string data;
		std::getline(stat, data);
		auto indexOfLastParenthesis = data.rfind(')');
		auto indexOfStatusIndicator = indexOfLastParenthesis + 2;
		return data[indexOfStatusIndicator];
	}
}

TEST_CASE("Process::Launch success", "[process]")
{
	auto proc = Process::Launch("yes");
	REQUIRE(ProcessExists(proc->Pid()));
}

TEST_CASE("Process::Launch no such program", "[process]")
{
	REQUIRE_THROWS_AS(Process::Launch("you_do_not_have_to_be_good"), Error);
}

#include <iostream>
TEST_CASE("Process::Attach success", "[process]")
{
	auto target = Process::Launch("targets/run_endlessly", false);
	auto proc = Process::Attach(target->Pid());
	REQUIRE(GetProcessStatus(target->Pid()) == 't');
}

TEST_CASE("Process::Attach invalid PID", "[process]")
{
	REQUIRE_THROWS_AS(Process::Attach(0), Error);
}