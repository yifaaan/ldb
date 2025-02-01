#include "libldb/types.hpp"
#include <fmt/base.h>
#include <fstream>
#include <regex>
#include <cerrno>
#include <sys/types.h>
#include <signal.h>
#include <elf.h>
#include <fcntl.h>

#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>

#include <libldb/process.hpp>
#include <libldb/error.hpp>
#include <libldb/pipe.hpp>
#include <libldb/register_info.hpp>
#include <libldb/bit.hpp>
#include <libldb/syscalls.hpp>
#include <libldb/target.hpp>

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

    /// parse the out put of command: readelf -WS <program_path> 
    std::int64_t GetSectionLoadBias(std::filesystem::path path, Elf64_Addr fileAddress)
    {
        auto command = fmt::format("readelf -WS {}", path.string());
        auto pipe = popen(command.c_str(), "r");

        std::regex textRegex(R"(PROGBITS\s+(\w+)\s+(\w+)\s+(\w+))");
        char* line = nullptr;
        std::size_t len = 0;

        while (getline(&line, &len, pipe) != -1)
        {
            std::cmatch groups;
            if (std::regex_search(line, groups, textRegex))
            {
                auto address = std::stol(groups[1], nullptr, 16);
                auto offset = std::stol(groups[2], nullptr, 16);
                auto size = std::stol(groups[3], nullptr, 16);
                if (address <= fileAddress and fileAddress < (address + size))
                {
                    free(line);
                    pclose(pipe);
                    return address - offset;
                }
            }
            free(line);
            line = nullptr;
        }
        pclose(pipe);
        ldb::Error::Send("Could not find section load bias");
    }


    /// get the offset of the entry point 
    ///
    /// how to calculate the offset of entry point?
    /// for example, entry file address is 0x400080
    /// 
    /// Name    Type        Address     Off    Size
    ///
    /// .text	PROGBITS	0x400000	0x1000
    ///
    /// 0x400080 - (0x400000 - 0x1000) = 0x1080
    std::int64_t GetEntryPointOffset(std::filesystem::path path)
    {
        std::ifstream elfFile(path);

        Elf64_Ehdr header;
        elfFile.read(reinterpret_cast<char*>(&header), sizeof(header));

        auto entryFileAddress = header.e_entry;
        return entryFileAddress - GetSectionLoadBias(path, entryFileAddress);
    }

    /// parse /proc/<pid>/maps file to get load address
    ldb::VirtAddr GetLoadAddress(pid_t pid, std::int64_t offset)
    {
        std::ifstream maps(fmt::format("/proc/{}/maps", pid));

        //        start-end          type fileOffset
        // 55693cd2c000-55693cd2d000 r-xp 00001000 08:20 86486 /home/clyf/dev/ldb/build/test/targets/run_endlessly
        std::regex mapRegex(R"((\w+)-\w+ ..(.). (\w+))");

        std::string data;
        while (std::getline(maps, data))
        {
            std::smatch groups;
            std::regex_search(data, groups, mapRegex);

            // executable segment
            if (groups[2] == 'x')
            {
                auto lowRange = std::stol(groups[1], nullptr, 16);
                auto fileOffset = std::stol(groups[3], nullptr, 16);
                // entry point load virtual address = 
                // entry point file offset - segment file offset + segment load start virtual address
                return ldb::VirtAddr(offset - fileOffset + lowRange);
            }
        }
        ldb::Error::Send("Could not find load address");
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
}

TEST_CASE("Breakpoint on address works", "[breakpoint]")
{
    bool closeOnExec = false;
    Pipe channel(closeOnExec);

    auto program = "/home/clyf/dev/ldb/build/test/targets/hello_ldb";
    auto proc = Process::Launch(program, true, channel.GetWrite());
    channel.CloseWrite();

    auto offset = GetEntryPointOffset(program);
    auto loadAddress = GetLoadAddress(proc->Pid(), offset);

    proc->CreateBreakpointSite(loadAddress).Enable();
    proc->Resume();
    auto reason = proc->WaitOnSignal();

    REQUIRE(reason.reason == ProcessState::Stopped);
    REQUIRE(reason.info == SIGTRAP);
    REQUIRE(proc->GetPc() == loadAddress);

    proc->Resume();
    reason = proc->WaitOnSignal();

    REQUIRE(reason.reason == ProcessState::Exited);
    REQUIRE(reason.info == 0);

    auto data = channel.Read();
    REQUIRE(ToStringView(data) == "Hello, ldb!\n");
}

TEST_CASE("Can remove breakpoint sites", "[breakpoint]")
{
    auto program = "/home/clyf/dev/ldb/build/test/targets/run_endlessly";
    auto proc = Process::Launch(program);

    auto& site = proc->CreateBreakpointSite(VirtAddr{42});
    proc->CreateBreakpointSite(VirtAddr{43});
    REQUIRE(proc->BreakPointSites().Size() == 2);

    proc->BreakPointSites().RemoveById(site.Id());
    proc->BreakPointSites().RemoveByAddress(VirtAddr{43});
    REQUIRE(proc->BreakPointSites().Empty());
}

TEST_CASE("Reading and writing memory works", "[memory]")
{
    bool closeOnExec = false;
    Pipe channel(closeOnExec);

    auto program = "/home/clyf/dev/ldb/build/test/targets/memory";
    auto proc = Process::Launch(program, true, channel.GetWrite());
    channel.CloseWrite();

    proc->Resume();
    proc->WaitOnSignal();

    auto aPointer = FromBytes<std::uint64_t>(channel.Read().data());
    auto dataVec = proc->ReadMemory(VirtAddr{aPointer}, 8);
    auto data = FromBytes<std::uint64_t>(dataVec.data());
    REQUIRE(data == 0xcafecafe);

    proc->Resume();
    proc->WaitOnSignal();

    auto bPointer = FromBytes<std::uint64_t>(channel.Read().data());
    dataVec = proc->ReadMemory(VirtAddr{bPointer}, 8);
    data = FromBytes<std::uint64_t>(dataVec.data());
    REQUIRE(data == 0xaaaaaaaa);


    proc->Resume();
    proc->WaitOnSignal();
    auto cPointer = FromBytes<std::uint64_t>(channel.Read().data());
    proc->WriteMemory(VirtAddr{cPointer}, {AsBytes("Hello, ldb!"), 12});
    proc->Resume();
    proc->WaitOnSignal();

    auto read = channel.Read();
    REQUIRE(ToStringView(read) == "Hello, ldb!");
}

TEST_CASE("Hardware breakpoint evades memory checksums", "[breakpoint]")
{
    bool closeOnExec = false;
    Pipe channel(closeOnExec);

    auto program = "/home/clyf/dev/ldb/build/test/targets/anti_debugger";
    auto proc = Process::Launch(program, true, channel.GetWrite());
    channel.CloseWrite();

    proc->Resume();
    proc->WaitOnSignal();


    auto func = VirtAddr(FromBytes<std::uint64_t>(channel.Read().data()));

    fmt::println("{:#016x}", func.Addr());

    // change instruction
    auto& soft = proc->CreateBreakpointSite(func, false);
    soft.Enable();

    proc->Resume();
    proc->WaitOnSignal();
    // puts Putting pepperoni on pizza...
    REQUIRE(ToStringView(channel.Read()) == "Putting pepperoni on pizza...\n");
    // trap

    proc->BreakPointSites().RemoveById(soft.Id());
    auto& hard = proc->CreateBreakpointSite(func, true);
    hard.Enable();


    proc->Resume();
    proc->WaitOnSignal();
    // puts Putting pineapple on pizza...

    REQUIRE(proc->GetPc() == func);
    proc->Resume();
    proc->WaitOnSignal();

    REQUIRE(ToStringView(channel.Read()) == "Putting pineapple on pizza...\n");
}

TEST_CASE("Watchpoint detects read", "[watchpoint]")
{
    bool closeOnExec = false;
    Pipe channel(closeOnExec);

    auto program = "/home/clyf/dev/ldb/build/test/targets/anti_debugger";
    auto proc = Process::Launch(program, true, channel.GetWrite());
    channel.CloseWrite();

    proc->Resume();
    // anti_debugger: Raise(SIGTRAP)
    proc->WaitOnSignal();
    
    auto func = VirtAddr(FromBytes<std::uint64_t>(channel.Read().data()));

    // did not change the instruction(code text)
    auto& watch = proc->CreateWatchpoint(func, ldb::StoppointMode::ReadWrite, 1);
    watch.Enable();

    proc->Resume();
    // anti_debugger: AnInnocentFunction()
    auto reason = proc->WaitOnSignal();

    REQUIRE(reason.info == SIGTRAP);

    proc->Resume();
    // fflush(stdout);
    // raise(SIGTRAP);
    proc->WaitOnSignal();

    REQUIRE(ToStringView(channel.Read()) == "Putting pineapple on pizza...\n");
}

TEST_CASE("Syscall mapping works", "[syscall]")
{
    REQUIRE(ldb::SyscallIdToName(0) == "read");
    REQUIRE(ldb::SyscallNameToId("read") == 0);

    REQUIRE(ldb::SyscallIdToName(9) == "mmap");
    REQUIRE(ldb::SyscallNameToId("mmap") == 9);
}

TEST_CASE("Syscall catchpoints work", "[catchpoint]")
{
    auto devNull = open("/dev/null", O_WRONLY);
    auto program = "/home/clyf/dev/ldb/build/test/targets/anti_debugger";
    auto proc = Process::Launch(program, true, devNull);

    auto writeSyscallId = ldb::SyscallNameToId("write");
    auto policy = ldb::SyscallCatchPolicy::CatchSome({ writeSyscallId });
    proc->SetSyscallCatchPolicy(policy);

    proc->Resume();
    auto reason = proc->WaitOnSignal();


    // trap in: write(STDOUT_FILENO, &ptr, sizeof(void*));
    REQUIRE(reason.reason == ldb::ProcessState::Stopped);
    REQUIRE(reason.info == SIGTRAP);
    REQUIRE(reason.trapReason == ldb::TrapType::Syscall);
    REQUIRE(reason.syscallInfo->id == writeSyscallId);
    REQUIRE(reason.syscallInfo->entry == true);

    proc->Resume();
    reason = proc->WaitOnSignal();

    // trap in: write(STDOUT_FILENO, &ptr, sizeof(void*));
    REQUIRE(reason.reason == ldb::ProcessState::Stopped);
    REQUIRE(reason.info == SIGTRAP);
    REQUIRE(reason.trapReason == ldb::TrapType::Syscall);
    REQUIRE(reason.syscallInfo->id == writeSyscallId);
    REQUIRE(reason.syscallInfo->entry == false);

    close(devNull);
}


TEST_CASE("ELF parser works", "[elf]")
{
    auto path = "/home/clyf/dev/ldb/build/test/targets/hello_ldb";
    ldb::Elf elf{ path };
    auto entry = elf.GetHeader().e_entry;
    auto sym = elf.GetSymbolAtAddress(FileAddr{ elf, entry });
    auto name = elf.GetString(sym.value()->st_name);
    REQUIRE(name == "_start");
    auto syms = elf.GetSymbolsByName("_start");
    name = elf.GetString(syms.at(0)->st_name);
    REQUIRE(name == "_start");

    elf.NotifyLoaded(VirtAddr { 0xcafecafe });
    sym = elf.GetSymbolAtAddress(VirtAddr{ 0xcafecafe + entry });
    name = elf.GetString(sym.value()->st_name);
    REQUIRE(name == "_start");
}
