#include <fstream>

#include <catch2/catch_test_macros.hpp>

#include <libldb/Error.h>
#include <libldb/Pipe.h>
#include <libldb/Process.h>
#include <libldb/bit.h>

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

TEST_CASE("Process::Attach success", "[Process]")
{
    auto target = Process::Launch("targets/RunEndlessly", false);
    auto process = Process::Attach(target->Pid());
    REQUIRE(GetProcessStatus(target->Pid()) == 't');
}

TEST_CASE("Process::Attach invalid PID", "[Process]")
{
    REQUIRE_THROWS_AS(Process::Attach(0), Error);
}

TEST_CASE("Process:Resume success", "[Process]")
{
    {
        auto process = Process::Launch("targets/RunEndlessly");
        process->Resume();
        auto status = GetProcessStatus(process->Pid());
        auto success = status == 'R' || status == 'S';
        REQUIRE(success);
    }

    {
        auto target = Process::Launch("targets/RunEndlessly", false);
        auto process = Process::Attach(target->Pid());
        process->Resume();
        auto status = GetProcessStatus(process->Pid());
        auto success = status == 'R' || status == 'S';
        REQUIRE(success);
    }
}

TEST_CASE("Process::Resume already running", "[Process]")
{
    auto process = Process::Launch("targets/EndImmediately");
    process->Resume();
    process->WaitOnSignal();
    REQUIRE_THROWS_AS(process->Resume(), Error);
}

TEST_CASE("Write register works", "[Register]")
{
    bool closeOnExec = false;
    Pipe channel(closeOnExec);
    auto process = Process::Launch("targets/RegWrite", true, channel.GetWrite());
    channel.CloseWrite();
    process->Resume();
    process->WaitOnSignal();
    auto& regs = process->GetRegisters();
    regs.WriteById(RegisterId::rsi, 0xcafecafe);
    process->Resume();
    process->WaitOnSignal();
    auto output = channel.Read();
    REQUIRE(ToStringView(output) == "0xcafecafe");

    regs.WriteById(RegisterId::mm0, 0xba5eba11);
    process->Resume();
    process->WaitOnSignal();
    output = channel.Read();
    REQUIRE(ToStringView(output) == "0xba5eba11");

    regs.WriteById(RegisterId::xmm0, 42.24);
    process->Resume();
    process->WaitOnSignal();
    output = channel.Read();
    REQUIRE(ToStringView(output) == "42.24");

    regs.WriteById(RegisterId::st0, 42.24l);
    regs.WriteById(RegisterId::fsw, uint16_t(0b0011100000000000));
    regs.WriteById(RegisterId::ftw, std::uint16_t{0b0011111111111111});
    process->Resume();
    process->WaitOnSignal();
    output = channel.Read();
    REQUIRE(ToStringView(output) == "42.24");
}

TEST_CASE("Read register works", "[Register]")
{
    auto process = Process::Launch("targets/RegRead");
    auto& regs = process->GetRegisters();

    process->Resume();
    process->WaitOnSignal();
    REQUIRE(regs.ReadByIdAs<uint64_t>(RegisterId::r13) == 0xcafecafe);

    process->Resume();
    process->WaitOnSignal();
    REQUIRE(regs.ReadByIdAs<uint8_t>(RegisterId::r13b) == 42);
    REQUIRE(regs.ReadByIdAs<uint64_t>(RegisterId::r13) == 0xcafeca2a);

    process->Resume();
    process->WaitOnSignal();
    REQUIRE(regs.ReadByIdAs<Byte64>(RegisterId::mm0) == ToByte64(0xba5eba11ull));

    process->Resume();
    process->WaitOnSignal();
    REQUIRE(regs.ReadByIdAs<Byte128>(RegisterId::xmm0) == ToByte128(64.125));

    process->Resume();
    process->WaitOnSignal();
    REQUIRE(regs.ReadByIdAs<long double>(RegisterId::st0) == 64.125L);
}

TEST_CASE("Can create breakpoint site", "[Breakpoint]")
{
    auto process = Process::Launch("targets/RunEndlessly");
    auto& site = process->CreateBreakpointSite(VirtAddr(42));
    REQUIRE(site.Address().Address() == 42);
}

TEST_CASE("Breakpoint site ids increase", "[Breakpoint]")
{
    auto process = Process::Launch("targets/RunEndlessly");
    auto& s1 = process->CreateBreakpointSite(VirtAddr(42));
    REQUIRE(s1.Address().Address() == 42);
    auto& s2 = process->CreateBreakpointSite(VirtAddr(43));
    REQUIRE(s2.Id() == s1.Id() + 1);
    auto& s3 = process->CreateBreakpointSite(VirtAddr(44));
    REQUIRE(s3.Id() == s2.Id() + 1);
    auto& s4 = process->CreateBreakpointSite(VirtAddr(45));
    REQUIRE(s4.Id() == s3.Id() + 1);
}

TEST_CASE("Can find breakpoint site", "[Breakpoint]")
{
    auto process = Process::Launch("targets/RunEndlessly");
    const auto& cprocess = process;
    process->CreateBreakpointSite(VirtAddr(42));
    process->CreateBreakpointSite(VirtAddr(43));
    process->CreateBreakpointSite(VirtAddr(44));
    process->CreateBreakpointSite(VirtAddr(45));

    auto& s1 = process->BreakpointSites().GetByAddress(VirtAddr(44));
    REQUIRE(process->BreakpointSites().ContainsAddress(VirtAddr(44)));
    REQUIRE(s1.Address().Address() == 44);

    auto& cs1 = cprocess->BreakpointSites().GetByAddress(VirtAddr(44));
    REQUIRE(cprocess->BreakpointSites().ContainsAddress(VirtAddr(44)));
    REQUIRE(cs1.Address().Address() == 44);

    auto& s2 = process->BreakpointSites().GetById(s1.Id() + 1);
    REQUIRE(process->BreakpointSites().ContainsId(s1.Id() + 1));
    REQUIRE(s2.Id() == s1.Id() + 1);
    REQUIRE(s2.Address().Address() == 45);

    auto& cs2 = process->BreakpointSites().GetById(cs1.Id() + 1);
    REQUIRE(process->BreakpointSites().ContainsId(cs1.Id() + 1));
    REQUIRE(cs2.Id() == cs1.Id() + 1);
    REQUIRE(cs2.Address().Address() == 45);
}

TEST_CASE("Can iterate breakpoint sites", "[breakpoint]")
{
    auto process = Process::Launch("targets/RunEndlessly");
    const auto& cprocess = process;

    process->CreateBreakpointSite(VirtAddr(42));
    process->CreateBreakpointSite(VirtAddr(43));
    process->CreateBreakpointSite(VirtAddr(44));
    process->CreateBreakpointSite(VirtAddr(45));

    process->BreakpointSites().ForEach(
    [addr = 42](auto& site) mutable
    {
        REQUIRE(site.Address().Address() == addr++);
    });

    cprocess->BreakpointSites().ForEach(
    [addr = 42](auto& site) mutable
    {
        REQUIRE(site.Address().Address() == addr++);
    });
}