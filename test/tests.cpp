#include <sys/types.h>
#include <elf.h>
#include <signal.h>

#include <fstream>
#include <format>
#include <regex>
#include <iostream>

#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>
#include <fmt/ranges.h>

#include <libldb/process.hpp>
#include <libldb/error.hpp>
#include <libldb/pipe.hpp>
#include <libldb/bit.hpp>
#include <libldb/syscall.hpp>

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

	std::uint64_t GetSectionLoadBias(std::filesystem::path path, Elf64_Addr fileAddress)
	{
		auto command = std::string{ "readelf -WS " } + path.string();
		auto pipe = popen(command.c_str(), "r");
		// [16] .text             PROGBITS        0000000000001060 001060 000107 00  AX  0   0 16
		std::regex textReg{ R"(PROGBITS\s+(\w+)\s+(\w+)\s+(\w+))" };
		char* line = nullptr;
		std::size_t len = 0;
		while (getline(&line, &len, pipe) != -1)
		{
			std::cmatch groups;
			if (std::regex_search(line, groups, textReg))
			{
				// file address
				auto address = std::stol(groups[1], nullptr, 16);
				// file offset
				auto offset = std::stol(groups[2], nullptr, 16);
				// section size
				auto size = std::stol(groups[3], nullptr, 16);
				if (address <= fileAddress && fileAddress < (address + size))
				{
					auto sectionLoadBias = address - offset;
					free(line);
					pclose(pipe);
					return sectionLoadBias;
				}
			}
			free(line);
			line = nullptr;
		}
		pclose(pipe);
		Error::Send("Could not find section load bias");
	}

	std::int64_t GetEntryPointOffset(std::filesystem::path path)
	{
		std::ifstream elf{ path };
		Elf64_Ehdr header;
		elf.read(reinterpret_cast<char*>(&header), sizeof(header));
		auto entryPointFileAddress = header.e_entry;
		return entryPointFileAddress - GetSectionLoadBias(path, entryPointFileAddress);
	}

	/// <summary>
	/// get the entry point load address
	/// </summary>
	/// <param name="pid"></param>
	/// <param name="offset">the file offset of entry point</param>
	/// <returns></returns>
	VirtAddr GetLoadAddress(pid_t pid, std::int64_t offset)
	{
		std::ifstream maps{ std::format("/proc/{}/maps", pid) };
		std::regex mapReg{ R"((\w+)-\w+ ..(.). (\w+))" };
		std::string data;
		while (std::getline(maps, data))
		{
			std::smatch groups;
			std::regex_search(data, groups, mapReg);
			// .text
			if (groups[2] == 'x')
			{
				auto low = std::stol(groups[1], nullptr, 16);
				auto fileOffset = std::stol(groups[3], nullptr, 16);
				return VirtAddr{ static_cast<std::uint64_t>(offset - fileOffset + low)};
			}
		}
		Error::Send("Could not find load address");
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

TEST_CASE("Process::Resume success", "[process]")
{
	{
		auto proc = Process::Launch("targets/run_endlessly");
		auto status = GetProcessStatus(proc->Pid());
		// tracing stop
		REQUIRE((status == 't'));

		proc->Resume();
		status = GetProcessStatus(proc->Pid());
		// running or sleeping in an interruptible wait
		REQUIRE((status == 'R' || status == 'S'));
	}

	{
		auto target = Process::Launch("targets/run_endlessly", false);
		auto status = GetProcessStatus(target->Pid());
		REQUIRE((status == 'R' || status == 'S'));

		auto proc = Process::Attach(target->Pid());
		status = GetProcessStatus(target->Pid());
		REQUIRE((status == 't'));
		proc->Resume();
		status = GetProcessStatus(target->Pid());
		REQUIRE((status == 'R' || status == 'S'));
	}
}

TEST_CASE("Process::Resume already terminated", "[process]")
{
	auto proc = Process::Launch("targets/end_immediately");
	// tracing stop then resume and end immediately
	proc->Resume();
	// terminated
	proc->WaitOnSignal();
	REQUIRE_THROWS_AS(proc->Resume(), Error);
}

TEST_CASE("Write register works", "[register]")
{
	bool closeOnExec = false;
	Pipe channel{ closeOnExec };

	auto proc = Process::Launch("targets/reg_write", true, channel.GetWrite());
	channel.CloseWrite();

	proc->Resume();
	proc->WaitOnSignal();

	// self trap by call kill


	// rsi
	auto& regs = proc->GetRegisters();
	regs.WriteById(RegisterId::rsi, 0xcafecafe);
	proc->Resume(); // print the contents of rsi then trap
	proc->WaitOnSignal();
	auto output = channel.Read();
	REQUIRE(ToStringView(output) == "0xcafecafe");



	// mm0
	regs.WriteById(RegisterId::mm0, 0xccaaff);
	proc->Resume(); // print the contents of mm0 then trap
	proc->WaitOnSignal();
	output = channel.Read();
	REQUIRE(ToStringView(output) ==  "0xccaaff");

	// xmm0
	regs.WriteById(RegisterId::xmm0, 42.2434);
	proc->Resume(); // print the contents of xmm0 then trap
	proc->WaitOnSignal();
	output = channel.Read();
	REQUIRE(ToStringView(output) == "42.2434");

	// x87
	regs.WriteById(RegisterId::st0, 42.24l);
	regs.WriteById(RegisterId::fsw, std::uint16_t{ 0b0011100000000000 });
	regs.WriteById(RegisterId::ftw, std::uint16_t{ 0b0011111111111111 });
	proc->Resume(); // print the contents of st0 then trap
	proc->WaitOnSignal();
	output = channel.Read();
	REQUIRE(ToStringView(output) == "42.24");
}

TEST_CASE("Read register works", "[register]")
{
	auto proc = Process::Launch("targets/reg_read");
	auto& regs = proc->GetRegisters();
	proc->Resume();
	proc->WaitOnSignal();
	// first trap

	// r13
	REQUIRE(regs.ReadByIdAs<std::uint64_t>(RegisterId::r13) == 0xcafecafe);
	proc->Resume();
	proc->WaitOnSignal();
	
	// r13b
	REQUIRE(regs.ReadByIdAs<std::uint8_t>(RegisterId::r13b) == 42);
	proc->Resume();
	proc->WaitOnSignal();

	// mm0
	REQUIRE(regs.ReadByIdAs<Byte64>(RegisterId::mm0) == ToByte64(0xba5eba11ull));
	proc->Resume();
	proc->WaitOnSignal();

	// xmm0
	REQUIRE(regs.ReadByIdAs<Byte128>(RegisterId::xmm0) == ToByte128(64.125));
	proc->Resume();
	proc->WaitOnSignal();

	// st0
	REQUIRE(regs.ReadByIdAs<long double>(RegisterId::st0) == 64.125L);
}

TEST_CASE("Can create breakpoint site", "[breakpoint]")
{
	auto proc = Process::Launch("targets/run_endlessly");
	auto& site = proc->CreateBreakpointSite(VirtAddr{ 42 });
	REQUIRE(site.Address().Addr() == 42);
}

TEST_CASE("Breakpoint site ids increase", "[breakpoint]")
{
	auto proc = Process::Launch("targets/run_endlessly");

	auto& s1 = proc->CreateBreakpointSite(VirtAddr{ 42 });
	REQUIRE(s1.Address().Addr() == 42);
	auto& s2 = proc->CreateBreakpointSite(VirtAddr{ 43 });
	REQUIRE(s2.Id() == s1.Id() + 1);
	auto& s3 = proc->CreateBreakpointSite(VirtAddr{ 44 });
	REQUIRE(s3.Id() == s1.Id() + 2);
	auto& s4 = proc->CreateBreakpointSite(VirtAddr{ 45 });
	REQUIRE(s4.Id() == s1.Id() + 3);
}

TEST_CASE("Can find breakpoint site", "[breakpoint]")
{
	auto proc = Process::Launch("targets/run_endlessly");
	const auto& cproc = *proc;

	proc->CreateBreakpointSite(VirtAddr{ 42 });
	proc->CreateBreakpointSite(VirtAddr{ 43 });
	proc->CreateBreakpointSite(VirtAddr{ 44 });
	proc->CreateBreakpointSite(VirtAddr{ 45 });

	auto& s1 = proc->BreakpointSites().GetByAddress(VirtAddr{ 44 });
	REQUIRE(proc->BreakpointSites().ContainsAddress(VirtAddr{ 44 }));
	REQUIRE(s1.Address().Addr() == 44);

	auto& cs1 = cproc.BreakpointSites().GetByAddress(VirtAddr{ 44 });
	REQUIRE(cproc.BreakpointSites().ContainsAddress(VirtAddr{ 44 }));
	REQUIRE(cs1.Address().Addr() == 44);

	auto& s2 = proc->BreakpointSites().GetById(s1.Id() + 1);
	REQUIRE(proc->BreakpointSites().ContainsId(s1.Id() + 1));
	REQUIRE(s2.Id() == s1.Id() + 1);
	REQUIRE(s2.Address().Addr() == 45);

	auto& cs2 = proc->BreakpointSites().GetById(s1.Id() + 1);
	REQUIRE(cproc.BreakpointSites().ContainsId(s1.Id() + 1));
	REQUIRE(cs2.Id() == cs1.Id() + 1);
	REQUIRE(cs2.Address().Addr() == 45);
}

TEST_CASE("Cannot find breakpoint site", "[breakpoint]")
{
	auto proc = Process::Launch("targets/run_endlessly");
	const auto& cproc = *proc;

	REQUIRE_THROWS_AS(proc->BreakpointSites().GetByAddress(VirtAddr{ 42 }), Error);
	REQUIRE_THROWS_AS(proc->BreakpointSites().GetById(42), Error);
	REQUIRE_THROWS_AS(cproc.BreakpointSites().GetByAddress(VirtAddr{ 42 }), Error);
	REQUIRE_THROWS_AS(cproc.BreakpointSites().GetById(42), Error);
}

TEST_CASE("Breakpoint site list size and emptiness", "[breakpoint]")
{
	auto proc = Process::Launch("targets/run_endlessly");
	const auto& cproc = *proc;

	REQUIRE(proc->BreakpointSites().Size() == 0);
	REQUIRE(proc->BreakpointSites().Empty());
	REQUIRE(cproc.BreakpointSites().Size() == 0);
	REQUIRE(cproc.BreakpointSites().Empty());

	proc->CreateBreakpointSite(VirtAddr{ 42 });
	REQUIRE(!proc->BreakpointSites().Empty());
	REQUIRE(proc->BreakpointSites().Size() == 1);
	REQUIRE(!cproc.BreakpointSites().Empty());
	REQUIRE(cproc.BreakpointSites().Size() == 1);

	proc->CreateBreakpointSite(VirtAddr{ 43 });
	REQUIRE(!proc->BreakpointSites().Empty());
	REQUIRE(proc->BreakpointSites().Size() == 2);
	REQUIRE(!cproc.BreakpointSites().Empty());
	REQUIRE(cproc.BreakpointSites().Size() == 2);
}

TEST_CASE("Can iterate breakpoint sites", "[breakpoint]")
{
	auto proc = Process::Launch("targets/run_endlessly");
	const auto& cproc = *proc;

	proc->CreateBreakpointSite(VirtAddr{ 42 });
	proc->CreateBreakpointSite(VirtAddr{ 43 });
	proc->CreateBreakpointSite(VirtAddr{ 44 });
	proc->CreateBreakpointSite(VirtAddr{ 45 });

	proc->BreakpointSites().ForEach([addr = 42](auto& site) mutable
	{
		REQUIRE(site.Address().Addr() == addr++);
	});

	cproc.BreakpointSites().ForEach([addr = 42](auto& site) mutable
	{
		REQUIRE(site.Address().Addr() == addr++);
	});
}

TEST_CASE("Breakpoint on address works", "[breakpoint]")
{
	bool closeOnExec = false;
	Pipe channel{ closeOnExec };
	auto proc = Process::Launch("targets/hello_ldb", true, channel.GetWrite());
	channel.CloseWrite();
	auto offset = GetEntryPointOffset("targets/hello_ldb");
	auto loadAddress = GetLoadAddress(proc->Pid(), offset);
	proc->CreateBreakpointSite(loadAddress).Enable();
	proc->Resume();
	auto reason = proc->WaitOnSignal();
	
	REQUIRE(reason.reason == ProcessState::stopped);
	REQUIRE(reason.info == SIGTRAP);
	REQUIRE(proc->GetPc() == loadAddress);
	
	proc->Resume();
	reason = proc->WaitOnSignal();
	REQUIRE(reason.reason == ProcessState::exited);
	REQUIRE(reason.info == 0);
	auto data = channel.Read();
	REQUIRE(ToStringView(data) == "Hello, ldb!\n");
}

TEST_CASE("Can remove breakpoint sites", "[breakpoint]")
{
	auto proc = Process::Launch("targets/run_endlessly");
	auto& site = proc->CreateBreakpointSite(VirtAddr{ 42 });
	proc->CreateBreakpointSite(VirtAddr{ 43 });
	REQUIRE(proc->BreakpointSites().Size() == 2);
	proc->BreakpointSites().RemoveById(site.Id());
	proc->BreakpointSites().RemoveByAddress(VirtAddr{ 43 });
	REQUIRE(proc->BreakpointSites().Empty());
}

TEST_CASE("Reading and writing memory works", "[memory]")
{
	bool closeOnExec = false;
	Pipe channel{ closeOnExec };
	auto proc = Process::Launch("targets/memory", true, channel.GetWrite());
	channel.CloseWrite();
	proc->Resume();
	proc->WaitOnSignal();
	auto aPointer = FromBytes<std::uint64_t>(channel.Read().data());
	auto dataVec = proc->ReadMemory(VirtAddr{ aPointer }, 8);
	//fmt::print("dataVec: {:02x}\n", fmt::join(dataVec, " "));
	auto data = FromBytes<std::uint64_t>(dataVec.data());
	REQUIRE(data == 0xcafecafe);

	proc->Resume();
	proc->WaitOnSignal();
	auto bPointer = FromBytes<std::uint64_t>(channel.Read().data());
	proc->WriteMemory(VirtAddr{ bPointer }, { AsBytes("hello, ldb!"), 12 });
	proc->Resume();
	proc->WaitOnSignal();
	auto read = channel.Read();
	REQUIRE(ToStringView(read) == "hello, ldb!");
}

TEST_CASE("Hardware breakpoint evades memory checksums", "[breakpoint]")
{
	bool closeOnExec = false;
	Pipe channel{ closeOnExec };
	auto proc = Process::Launch("targets/anti_debugger", true, channel.GetWrite());
	channel.CloseWrite();
	proc->Resume();
	proc->WaitOnSignal();

	auto func = VirtAddr{ FromBytes<std::uint64_t>(channel.Read().data()) };
	auto& soft = proc->CreateBreakpointSite(func, false);
	soft.Enable();
	proc->Resume();
	proc->WaitOnSignal();
	REQUIRE(ToStringView(channel.Read()) == "Putting pepperoni on pizza...\n");

	proc->BreakpointSites().RemoveById(soft.Id());
	auto& hard = proc->CreateBreakpointSite(func, true);
	hard.Enable();
	proc->Resume();
	proc->WaitOnSignal();
	REQUIRE(proc->GetPc() == func);

	proc->Resume();
	proc->WaitOnSignal();
	REQUIRE(ToStringView(channel.Read()) == "Putting pineapple on pizza...\n");
}

TEST_CASE("Watchpoint detects read", "[watchpoint]")
{
	bool closeOnExec = false;
	Pipe channel{ closeOnExec };
	auto proc = Process::Launch("targets/anti_debugger", true, channel.GetWrite());
	channel.CloseWrite();
	proc->Resume();
	// write fn addr to stdout then trap
	proc->WaitOnSignal();

	auto func = VirtAddr{ FromBytes<std::uint64_t>(channel.Read().data()) };
	auto& watch = proc->CreateWatchpoint(func, StoppointMode::readWrite, 1);
	watch.Enable();

	proc->Resume();
	// watchpoint trap
	proc->WaitOnSignal();
	proc->StepInstruction();
	// std::puts("Putting pineapple on pizza..."); then trap

	auto& soft = proc->CreateBreakpointSite(func, false);
	soft.Enable();

	proc->Resume();
	// while:: raise(TRAP)
	auto reason = proc->WaitOnSignal();
	REQUIRE(reason.info == SIGTRAP);

	proc->Resume();
	proc->WaitOnSignal();
	REQUIRE(ToStringView(channel.Read()) == "Putting pineapple on pizza...\n");
}

TEST_CASE("Syscall mapping works", "[syscall]")
{
	REQUIRE(SyscallIdToName(0) == "read");
	REQUIRE(SyscallNameToId("read") == 0);
	REQUIRE(SyscallIdToName(1) == "write");
	REQUIRE(SyscallNameToId("write") == 1);
	REQUIRE(SyscallIdToName(2) == "open");
	REQUIRE(SyscallNameToId("open") == 2);
	REQUIRE(SyscallIdToName(3) == "close");
	REQUIRE(SyscallNameToId("close") == 3);
}
