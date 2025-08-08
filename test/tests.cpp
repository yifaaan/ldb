#include <fstream>

#include <catch2/catch_test_macros.hpp>

#include <libldb/Error.h>
#include <libldb/Process.h>

#include <signal.h>
#include <sys/types.h>

using namespace ldb;

namespace
{
    bool ProcessExists(pid_t pid)
    {
        auto ret = kill(pid, 0);
        return ret != -1 && errno != ESRCH;
    }

    char GetProcessStatus(pid_t pid)
    {
        std::ifstream stat("/proc/" + std::to_string(pid) + "/stat");
        std::string data;
        std::getline(stat, data);
        auto indexOfLastParenthesis = data.rfind(')');
        auto indexOfStatusIndicator = indexOfLastParenthesis + 2;
        return data[indexOfStatusIndicator];
    }
} // namespace

TEST_CASE("Process::Launch success", "[Process]")
{
    auto process = Process::Launch("yes");
    REQUIRE(ProcessExists(process->Pid()));
}

TEST_CASE("Process::Launch no such program", "[Process]")
{
    REQUIRE_THROWS_AS(Process::Launch("you_do_not_have_to_be_good"), Error);
}
