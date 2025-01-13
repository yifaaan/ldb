#include <cerrno>
#include <sys/types.h>
#include <signal.h>
#include <fstream>

#include <catch2/catch_test_macros.hpp>
#include <libldb/process.hpp>
#include <libldb/error.hpp>
#include <libldb/pipe.hpp>
#include <libldb/register_info.hpp>
#include <libldb/bit.hpp>

namespace
{
    bool ProcessExists(pid_t pid)
    {
        auto ret = kill(pid, 0);
        return ret != -1 and errno != ESRCH;
    }

    char GetProcessStatus(pid_t pid)
    {
        std::ifstream stat("/proc/" + std::to_string(pid) + "/stat");
        std::string data;
        std::getline(stat, data);
        // clyf@DESKTOP-AVOKPOR:~/CodeSpace/ldb/build$ cat /proc/1/stat
        // 1 (systemd) S 0 1 1 0 -1 4194560 9829 618418 118 4376 78 58 4118 
        // 3104 20 0 1 0 202 22331392 3287 18446744073709551615 1 1 0 0 0 0 671173123 4096 1260 0 0 0 17 4 0 0 0 0 0 0 0 0 0 0 0 0 0
        auto indexOfLastParenthesis = data.rfind(')');
        auto indexOfStatusIndicator = indexOfLastParenthesis + 2;
        return data[indexOfStatusIndicator];
    }
}

TEST_CASE("Process::Launch success", "[process]")
{
    auto proc = ldb::Process::Launch("yes");
    REQUIRE(ProcessExists(proc->Pid()));
}

TEST_CASE("Process::Launch no such program", "[process]")
{
    REQUIRE_THROWS_AS(ldb::Process::Launch("fff"), ldb::Error);
}

TEST_CASE("Process::Attach success", "[process]")
{
    auto target = ldb::Process::Launch("/home/clyf/Workspace/ldb/build/test/targets/run_endlessly", false);
    auto proc = ldb::Process::Attach(target->Pid());
    REQUIRE(GetProcessStatus(target->Pid()) == 't');
}

TEST_CASE("Process::Attach invalid PID", "[process]")
{
    REQUIRE_THROWS_AS(ldb::Process::Attach(0), ldb::Error);
}

TEST_CASE("Process::Resume success", "[process]")
{
    {
        auto proc = ldb::Process::Launch("/home/clyf/Workspace/ldb/build/test/targets/run_endlessly");
        proc->Resume();
        auto status = GetProcessStatus(proc->Pid());
        auto success = status == 'R' or status == 'S';
        REQUIRE(success);
    }
    {
        auto target = ldb::Process::Launch("/home/clyf/Workspace/ldb/build/test/targets/run_endlessly", false);
        auto proc = ldb::Process::Attach(target->Pid());
        proc->Resume();
        auto status = GetProcessStatus(proc->Pid());
        auto success = status == 'R' or status == 'S';
        REQUIRE(success);
    }
}

TEST_CASE("Process::Resum already terminated", "[process]")
{
    auto proc = ldb::Process::Launch("/home/clyf/Workspace/ldb/build/test/targets/end_immediately");
    proc->Resume();
    proc->WaitOnSignal();
    REQUIRE_THROWS_AS(proc->Resume(), ldb::Error);
}

TEST_CASE("Write register works", "[register]")
{
    bool closeOnExec = false;
    ldb::Pipe channel(closeOnExec);

    auto proc = ldb::Process::Launch("/home/clyf/Workspace/ldb/build/test/targets/reg_write", true, channel.GetWrite());
    channel.CloseWrite();

    // print rsi
    proc->Resume();
    // trap //
    proc->WaitOnSignal();
    auto& regs = proc->GetRegisters();
    regs.WriteById(ldb::RegisterId::rsi, 0xcafecafe);
    proc->Resume();
    // call printf then trap //
    proc->WaitOnSignal();
    auto output = channel.Read();
    REQUIRE(ldb::ToStringView(output) == "0xcafecafe");


    // print mm0
    regs.WriteById(ldb::RegisterId::mm0, 0xba5eba11);
    proc->Resume();
    // call printf then trap
    proc->WaitOnSignal();
    output = channel.Read();
    REQUIRE(ldb::ToStringView(output) == "0xba5eba11");
}