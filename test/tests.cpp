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

using namespace ldb;

TEST_CASE("Process::Launch success", "[process]")
{
    auto proc = Process::Launch("yes");
    REQUIRE(ProcessExists(proc->Pid()));
}

TEST_CASE("Process::Launch no such program", "[process]")
{
    REQUIRE_THROWS_AS(Process::Launch("fff"), Error);
}

TEST_CASE("Process::Attach success", "[process]")
{
    auto target = Process::Launch("/home/clyf/dev/ldb/build/test/targets/run_endlessly", false);
    auto proc = Process::Attach(target->Pid());
    REQUIRE(GetProcessStatus(target->Pid()) == 't');
}

TEST_CASE("Process::Attach invalid PID", "[process]")
{
    REQUIRE_THROWS_AS(Process::Attach(0), Error);
}

TEST_CASE("Process::Resume success", "[process]")
{
    {
        auto proc = Process::Launch("/home/clyf/dev/ldb/build/test/targets/run_endlessly");
        proc->Resume();
        auto status = GetProcessStatus(proc->Pid());
        auto success = status == 'R' or status == 'S';
        REQUIRE(success);
    }
    {
        auto target = Process::Launch("/home/clyf/dev/ldb/build/test/targets/run_endlessly", false);
        auto proc = Process::Attach(target->Pid());
        proc->Resume();
        auto status = GetProcessStatus(proc->Pid());
        auto success = status == 'R' or status == 'S';
        REQUIRE(success);
    }
}

TEST_CASE("Process::Resum already terminated", "[process]")
{
    auto proc = Process::Launch("/home/clyf/dev/ldb/build/test/targets/end_immediately");
    proc->Resume();
    proc->WaitOnSignal();
    REQUIRE_THROWS_AS(proc->Resume(), Error);
}

TEST_CASE("Write register works", "[register]")
{
    bool closeOnExec = false;
    Pipe channel(closeOnExec);

    auto proc = Process::Launch("/home/clyf/dev/ldb/build/test/targets/reg_write", true, channel.GetWrite());
    channel.CloseWrite();

    // print rsi
    proc->Resume();
    // trap //
    proc->WaitOnSignal();
    auto& regs = proc->GetRegisters();
    regs.WriteById(RegisterId::rsi, 0xcafecafe);
    proc->Resume();
    // call printf then trap //
    proc->WaitOnSignal();
    auto output = channel.Read();
    REQUIRE(ToStringView(output) == "0xcafecafe");


    // print mm0
    regs.WriteById(RegisterId::mm0, 0xba5eba11);
    proc->Resume();
    // call printf then trap
    proc->WaitOnSignal();
    output = channel.Read();
    REQUIRE(ToStringView(output) == "0xba5eba11");

    // print xmm0
    regs.WriteById(RegisterId::xmm0, 42.24);
    proc->Resume();
    // call printf then trap
    proc->WaitOnSignal();
    output = channel.Read();
    REQUIRE(ToStringView(output) == "42.24");

    // print st0
    regs.WriteById(RegisterId::st0, 42.24l);
    regs.WriteById(RegisterId::fsw, std::uint16_t{0b0011100000000000});
    regs.WriteById(RegisterId::ftw, std::uint16_t{0b0011111111111111});
    proc->Resume();
    // call printf then trap
    proc->WaitOnSignal();
    output = channel.Read();
    REQUIRE(ToStringView(output) == "42.24");
}

TEST_CASE("Read register works", "[register]")
{
    auto proc = Process::Launch("/home/clyf/dev/ldb/build/test/targets/reg_read");
    auto& regs = proc->GetRegisters();

    proc->Resume();
    proc->WaitOnSignal();
    REQUIRE(regs.ReadByIdAs<std::uint64_t>(RegisterId::r13) == 0xcafecafe);


    proc->Resume();
    proc->WaitOnSignal();
    REQUIRE(regs.ReadByIdAs<std::uint8_t>(RegisterId::r13b) == 42);

    proc->Resume();
    proc->WaitOnSignal();
    REQUIRE(regs.ReadByIdAs<byte64>(RegisterId::mm0) == ToByte64(0xba5eba11ull));

    proc->Resume();
    proc->WaitOnSignal();
    REQUIRE(regs.ReadByIdAs<byte128>(RegisterId::xmm0) == ToByte128(64.125));

    proc->Resume();
    proc->WaitOnSignal();
    REQUIRE(regs.ReadByIdAs<long double>(RegisterId::st0) == 64.125L);
}

TEST_CASE("Can create breakpoint site", "[breakpoint]")
{
    auto proc = Process::Launch("/home/clyf/dev/ldb/build/test/targets/run_endlessly");
    auto& site = proc->CreateBreakpointSite(VirtAddr{42});
    REQUIRE(site.Address().Addr() == 42);
}

TEST_CASE("Breakpoint site ids increase", "[breakpoint]")
{
    auto proc = Process::Launch("/home/clyf/dev/ldb/build/test/targets/run_endlessly");

    auto& s1 = proc->CreateBreakpointSite(VirtAddr{42});
    REQUIRE(s1.Address().Addr() == 42);

    auto& s2 = proc->CreateBreakpointSite(VirtAddr{43});
    REQUIRE(s2.Id() == s1.Id() + 1);

    auto& s3 = proc->CreateBreakpointSite(VirtAddr{44});
    REQUIRE(s3.Id() == s1.Id() + 2);

    auto& s4 = proc->CreateBreakpointSite(VirtAddr{45});
    REQUIRE(s4.Id() == s1.Id() + 3);
}

TEST_CASE("Can find breakpoint site", "[breakpoint]")
{
    auto proc = Process::Launch("/home/clyf/dev/ldb/build/test/targets/run_endlessly");
    const auto& cproc = proc;

    proc->CreateBreakpointSite(VirtAddr{42});
    proc->CreateBreakpointSite(VirtAddr{43});
    proc->CreateBreakpointSite(VirtAddr{44});
    proc->CreateBreakpointSite(VirtAddr{45});

    auto& s1 = proc->BreakPointSites().GetByAddress(VirtAddr{44});
    REQUIRE(proc->BreakPointSites().ContainsAddress(VirtAddr{44}));
    REQUIRE(s1.Address().Addr() == 44);

    auto& cs1 = cproc->BreakPointSites().GetByAddress(VirtAddr{44});
    REQUIRE(cproc->BreakPointSites().ContainsAddress(VirtAddr{44}));
    REQUIRE(cs1.Address().Addr() == 44);

    auto& s2 = proc->BreakPointSites().GetById(s1.Id() + 1);
    REQUIRE(proc->BreakPointSites().ContainsId(s1.Id() + 1));
    REQUIRE(s2.Id() == s1.Id() + 1);
    REQUIRE(s2.Address().Addr() == 45);

    auto& cs2 = proc->BreakPointSites().GetById(cs1.Id() + 1);
    REQUIRE(cproc->BreakPointSites().ContainsId(cs1.Id() + 1));
    REQUIRE(cs2.Id() == cs1.Id() + 1);
    REQUIRE(s2.Address().Addr() == 45);
}

TEST_CASE("Cannot find breakpoint site", "[breakpoint]") 
{
    auto proc = Process::Launch("/home/clyf/dev/ldb/build/test/targets/run_endlessly");
    const auto& cproc = proc;
    REQUIRE_THROWS_AS(proc->BreakPointSites().GetByAddress(VirtAddr{ 44 }), Error);
    REQUIRE_THROWS_AS(proc->BreakPointSites().GetById(44), Error);
    REQUIRE_THROWS_AS(cproc->BreakPointSites().GetByAddress(VirtAddr{44}), Error);
    REQUIRE_THROWS_AS(cproc->BreakPointSites().GetById(44), Error);
}

TEST_CASE("Breakpoint site list size and emptiness", "[breakpoint]")
{
    auto proc = Process::Launch("/home/clyf/dev/ldb/build/test/targets/run_endlessly");
    const auto& cproc = proc;

    REQUIRE(proc->BreakPointSites().Empty());
    REQUIRE(proc->BreakPointSites().Size() == 0);
    REQUIRE(cproc->BreakPointSites().Empty());
    REQUIRE(cproc->BreakPointSites().Size() == 0);

    proc->CreateBreakpointSite(VirtAddr{42});
    REQUIRE(!proc->BreakPointSites().Empty());
    REQUIRE(proc->BreakPointSites().Size() == 1);
    REQUIRE(!cproc->BreakPointSites().Empty());
    REQUIRE(cproc->BreakPointSites().Size() == 1);

    proc->CreateBreakpointSite(VirtAddr{43});
    REQUIRE(!proc->BreakPointSites().Empty());
    REQUIRE(proc->BreakPointSites().Size() == 2);
    REQUIRE(!cproc->BreakPointSites().Empty());
    REQUIRE(cproc->BreakPointSites().Size() == 2);
}

TEST_CASE("Can iterate breakpoint sites", "[breakpoint]")
{
    auto proc = Process::Launch("/home/clyf/dev/ldb/build/test/targets/run_endlessly");
    const auto& cproc = proc;

    proc->CreateBreakpointSite(VirtAddr{42});
    proc->CreateBreakpointSite(VirtAddr{43});
    proc->CreateBreakpointSite(VirtAddr{44});
    proc->CreateBreakpointSite(VirtAddr{45});

    proc->BreakPointSites().ForEach([addr = 42](auto& site) mutable
    {
        REQUIRE(site.Address().Addr() == addr++);
    });

    cproc->BreakPointSites().ForEach([addr = 42](auto& site) mutable
    {
        REQUIRE(site.Address().Addr() == addr++);
    });
    
    if constexpr (std::is_const_v<decltype(cproc)>)
    {
        REQUIRE(false);
    }
}