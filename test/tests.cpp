#include <sys/types.h>
#include <signal.h>

#include <fstream>
#include <format>

#include <catch2/catch_test_macros.hpp>

#include <libldb/process.hpp>
#include <libldb/error.hpp>
#include <libldb/pipe.hpp>
#include <libldb/bit.hpp>

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