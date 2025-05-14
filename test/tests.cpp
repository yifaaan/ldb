#include <catch2/catch_test_macros.hpp>
#include <csignal>
#include <fstream>
#include "libldb/error.hpp"
#include "libldb/process.hpp"

using namespace ldb;
namespace
{
    bool process_exists(pid_t pid)
    {
        auto ret = kill(pid, 0);
        return ret != -1 && errno != ESRCH;
    }

    char get_process_status(pid_t pid)
    {
        std::ifstream stat{"/proc/" + std::to_string(pid) + "/stat"};
        std::string data;
        std::getline(stat, data);
        auto index_of_last_parenthesis = data.rfind(')');
        auto index_of_status = index_of_last_parenthesis + 2;
        return data[index_of_status];
    }
}

TEST_CASE("validate environment")
{
    REQUIRE(true);
}

TEST_CASE("process::launch success", "[process]")
{
    auto proc = process::launch("yes");
    REQUIRE(process_exists(proc->pid()));
}

TEST_CASE("process::launch no such program", "[process]")
{
    REQUIRE_THROWS_AS(process::launch("no such program"), error);
}