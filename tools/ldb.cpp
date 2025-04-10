#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>

#include <iostream>
#include <string>
#include <vector>
#include <format>
#include <ranges>

#include <readline/readline.h>
#include <readline/history.h>

#include <fmt/format.h>
#include <fmt/ranges.h>

#include <libldb/process.hpp>
#include <libldb/error.hpp>
#include <libldb/parse.hpp>
#include <libldb/disassembler.hpp>


namespace
{
	std::vector<std::string_view> Split(std::string_view sv, char delim)
	{
		std::vector<std::string_view> splits;
		std::size_t index = 0;
		while (true)
		{
			auto delimIndex = sv.find(delim, index);
			if (delimIndex != std::string::npos)
			{
				splits.emplace_back(sv.substr(index, delimIndex - index));
				index = delimIndex + 1;
			}
			else
			{
				splits.emplace_back(sv.substr(index));
				break;
			}
		}
		return splits;
	}

	bool IsPrefix(std::string_view str, std::string_view of)
	{
		return of.starts_with(str);
	}

	ldb::Registers::Value ParseRegisterValue(const ldb::RegisterInfo& info, std::string_view text)
	{
		try
		{
			if (info.format == ldb::RegisterFormat::uint)
			{
				switch (info.size)
				{
				case 1: return ldb::ToIntegral<std::uint8_t>(text, 16).value();
				case 2: return ldb::ToIntegral<std::uint16_t>(text, 16).value();
				case 4: return ldb::ToIntegral<std::uint32_t>(text, 16).value();
				case 8: return ldb::ToIntegral<std::uint64_t>(text, 16).value();
				}
			}
			else if (info.format == ldb::RegisterFormat::doubleFloat)
			{
				return ldb::ToFloat<double>(text).value();
			}
			else if (info.format == ldb::RegisterFormat::longDouble)
			{
				return ldb::ToFloat<long double>(text).value();
			}
			else if (info.format == ldb::RegisterFormat::vector)
			{
				if (info.size == 8)
				{
					return ldb::ParseVector<8>(text);
				}
				else if (info.size == 16)
				{
					return ldb::ParseVector<16>(text);
				}
			}
		}
		catch (...)
		{

		}
		ldb::Error::Send("Invalid format");
	}

	void PrintStopReason(const ldb::Process& process, ldb::StopReason reason)
	{
		std::string message;
		switch (reason.reason)
		{
		case ldb::ProcessState::exited:
			message = fmt::format("exited with status {}", static_cast<int>(reason.info));
			break;
		case ldb::ProcessState::terminated:
			message = fmt::format("terminated with signal {}", sigabbrev_np(reason.info));
			break;
		case ldb::ProcessState::stopped:
			message = fmt::format("stopped with signal {} at {:#x}", sigabbrev_np(reason.info), process.GetPc().Addr());
			break;
		}
		fmt::print("Process {} {}\n", process.Pid(), message);
	}

	void PrintHelp(const std::vector<std::string_view>& args)
	{
		if (args.size() == 1)
		{
			std::cerr << R"(Available commands:
continue	- Resume the process
step		- Step over a single instruction
register	- Commands for operating on registers
breakpoint	- Commands for operating on breakpoints
memory		- Commands for operating on memory
disassemble	- Disassemble machine code to assembly
)";
		}
		else if (IsPrefix(args[1], "registers"))
		{
			std::cerr << R"(Available commands:
read
read <register>
read all
write <register> <value>
)";
		}
		else if (IsPrefix(args[1], "breakpoint"))
		{
			std::cerr << R"(Available commands:
list
delete <id>
disable <id>
enable <id>
set <address>
)";
		}
		else if (IsPrefix(args[1], "memory"))
		{
			std::cerr << R"(Available commands:
read <address>
read <address> <number of bytes>
write <address> <bytes>
)";
		}
		else if (IsPrefix(args[1], "disassemble"))
		{
			std::cerr << R"(Available options:
-c <number of instructions>
-a <start address>
)";
		}
		else
		{
			std::cerr << "No help available on that\n";
		}
	}

	void PrintDisassembly(ldb::Process& process, ldb::VirtAddr address, std::size_t nInstructions)
	{
		ldb::Disassembler dis{ process };
		auto instructions = dis.Disassemble(nInstructions, address);
		for (auto& instr : instructions)
		{
			fmt::print("{:#018x}: {}\n", instr.address.Addr(), instr.text);
		}
	}

	void HandleStop(ldb::Process& process, ldb::StopReason reason)
	{
		PrintStopReason(process, reason);
		if (reason.reason == ldb::ProcessState::stopped)
		{
			PrintDisassembly(process, process.GetPc(), 5);
		}
	}

	void HandleRegisterRead(ldb::Process& process, const std::vector<std::string_view>& args)
	{
		auto format = [](auto t)
		{
			if constexpr (std::is_floating_point_v<decltype(t)>)
			{
				return fmt::format("{}", t);
			}
			else if constexpr (std::is_integral_v<decltype(t)>)
			{
				return fmt::format("{:#0{}x}", t, sizeof(t) * 2 + 2);
			}
			else
			{
				return fmt::format("[{:#04x}]", fmt::join(t, ","));
			}
		};
		if (args.size() == 2 || (args.size() == 3 && args[2] == "all"))
		{
			for (const auto& info : ldb::registerInfos)
			{
				auto shouldPrint = (args.size() == 3 || info.type == ldb::RegisterType::gpr && info.name != "orig_rax");
				if (!shouldPrint) continue;
				auto value = process.GetRegisters().Read(info);
				fmt::print("{}:\t{}\n", info.name, std::visit(format, value));
			}
		}
		else if (args.size() == 3)
		{
			try
			{
				auto info = ldb::RegisterInfoByName(args[2]);
				auto value = process.GetRegisters().Read(info);
				fmt::print("{}:\t{}\n", info.name, std::visit(format, value));
			}
			catch (ldb::Error& err)
			{
				std::cerr << "No such register\n";
				return;
			}
		}
		else
		{
			PrintHelp({ "help", "register" });
		}
	}

	void HandleRegisterWrite(ldb::Process& process, const std::vector<std::string_view>& args)
	{
		if (args.size() != 4)
		{
			PrintHelp({ "help", "register" });
			return;
		}
		try
		{
			auto info = ldb::RegisterInfoByName(args[2]);
			auto value = ParseRegisterValue(info, args[3]);
			process.GetRegisters().Write(info, value);
		}
		catch (ldb::Error& err)
		{
			std::cerr << err.what() << '\n';
			return;
		}
	}

	void HandleRegisterCommand(ldb::Process& process, const std::vector<std::string_view>& args)
	{
		if (args.size() < 2)
		{
			PrintHelp({ "help", "register" });
			return;
		}

		if (IsPrefix(args[1], "read"))
		{
			HandleRegisterRead(process, args);
		}
		else if (IsPrefix(args[1], "write"))
		{
			HandleRegisterWrite(process, args);
		}
		else
		{
			PrintHelp({ "help", "register" });
		}
	}

	void HandleBreakpointCommand(ldb::Process& process, std::span<std::string_view> args)
	{
		if (args.size() < 2)
		{
			PrintHelp({ "help", "breakpoint" });
			return;
		}
		auto command = args[1];
		if (IsPrefix(command, "list"))
		{
			if (process.BreakpointSites().Empty())
			{
				fmt::print("No breakpoints set\n");
			}
			else
			{
				fmt::print("Current breakpoints:\n");
				process.BreakpointSites().ForEach([](const auto& site)
				{
					fmt::print("{}: address = {:#x}, {}\n", site.Id(), site.Address().Addr(), site.IsEnabled() ? "enabled" : "disabled");
				});
			}
			return;
		}

		if (args.size() < 3)
		{
			PrintHelp({ "help", "breakpoint" });
			return;
		}

		if (IsPrefix(command, "set"))
		{
			auto address = ldb::ToIntegral<std::uint64_t>(args[2], 16);
			if (!address)
			{
				fmt::print(stderr, "Breakpoint command expectes address in hexadecimal, prefixed with '0x'\n");
				return;
			}
			process.CreateBreakpointSite(ldb::VirtAddr{ *address }).Enable();
			return;
		}

		auto id = ldb::ToIntegral<ldb::BreakpointSite::IdType>(args[2]);
		if (!id)
		{
			std::cerr << "Command expects breakpoint id";
			return;
		}
		if (IsPrefix(command, "enable"))
		{
			process.BreakpointSites().GetById(*id).Enable();
		}
		else if (IsPrefix(command, "disable"))
		{
			process.BreakpointSites().GetById(*id).Disable();
		}
		else if (IsPrefix(command, "delete"))
		{
			process.BreakpointSites().RemoveById(*id);
		}
	}

	// memory read <address>
	// memory read <address> <number of bytes>
	void HandleMemoryReadCommand(ldb::Process& process, std::span<std::string_view> args)
	{
		auto address = ldb::ToIntegral<std::uint64_t>(args[2], 16);
		if (!address)
		{
			ldb::Error::Send("Invalid address format");
		}
		// default to read 32 bytes
		auto nBytes = 32;
		if (args.size() == 4)
		{
			auto bytesArg = ldb::ToIntegral<std::size_t>(args[3]);
			if (!bytesArg)
			{
				ldb::Error::Send("Invalid number of bytes");
			}
			nBytes = *bytesArg;
		}
		auto data = process.ReadMemory(ldb::VirtAddr{ *address }, nBytes);
		for (std::size_t i = 0; i < data.size(); i += 16)
		{
			auto start = data.begin() + i;
			auto end = data.begin() + std::min(i + 16, data.size());;
			fmt::print("{:#016x}: {:02x}\n", *address + i, fmt::join(start, end, " "));
		}
	}

	// memory write <address> <values>
	// mem write 0x555555555156 [0xff,0xff]
	void HandleMemoryWriteCommand(ldb::Process& process, std::span<std::string_view> args)
	{
		if (args.size() != 4)
		{
			PrintHelp({ "help", "memory" });
			return;
		}
		auto address = ldb::ToIntegral<std::uint64_t>(args[2], 16);
		if (!address)
		{
			ldb::Error::Send("Invalid address format");
		}
		auto data = ldb::ParseVector(args[3]);
		process.WriteMemory(ldb::VirtAddr{ *address }, data);
	}

	void HandleMemoryCommand(ldb::Process& process, std::span<std::string_view> args)
	{
		if (args.size() < 3)
		{
			PrintHelp({ "help", "memory" });
			return;
		}
		if (IsPrefix(args[1], "read"))
		{
			HandleMemoryReadCommand(process, args);
		}
		else if (IsPrefix(args[1], "write"))
		{
			HandleMemoryWriteCommand(process, args);
		}
		else
		{
			PrintHelp({ "help", "memory" });
		}
	}

	// disassemble -c <n_instructions> -a <address>
	void HandleDisassembleCommand(ldb::Process& process, std::span<std::string_view> args)
	{
		auto address = process.GetPc();
		std::size_t nInstructions = 5;
		auto it = args.begin() + 1;
		while (it != args.end())
		{
			if (*it == "-a" && it + 1 != args.end())
			{
				++it;
				auto optAddr = ldb::ToIntegral<std::uint64_t>(*it++, 16);
				if (!optAddr)
				{
					ldb::Error::Send("Invalid address format");
				}
				address = ldb::VirtAddr{ *optAddr };
			}
			else if (*it == "-c" && it + 1 != args.end())
			{
				++it;
				auto optN = ldb::ToIntegral<std::size_t>(*it++);
				if (!optN)
				{
					ldb::Error::Send("Invalid instruction count");
				}
				nInstructions = *optN;
			}
			else
			{
				PrintHelp({ "help", "disassemble" });
				return;
			}
		}
		PrintDisassembly(process, address, nInstructions);
	}

	void HandleCommand(std::unique_ptr<ldb::Process>& process, std::string_view line)
	{
		auto args = Split(line, ' ');
		auto command = args[0];

		if (IsPrefix(command, "continue"))
		{
			process->Resume();
			auto reason = process->WaitOnSignal();
			HandleStop(*process, reason);
		}
		else if (IsPrefix(command, "help"))
		{
			PrintHelp(args);
		}
		else if (IsPrefix(command, "register"))
		{
			HandleRegisterCommand(*process, args);
		}
		else if (IsPrefix(command, "breakpoint"))
		{
			HandleBreakpointCommand(*process, args);
		}
		else if (IsPrefix(command, "step"))
		{
			auto reason = process->StepInstruction();
			HandleStop(*process, reason);
		}
		else if (IsPrefix(command, "memory"))
		{
			HandleMemoryCommand(*process, args);
		}
		else if (IsPrefix(command, "disassemble"))
		{
			HandleDisassembleCommand(*process, args);
		}
		else
		{
			std::cerr << "Unknown command\n";
		}
	}

	std::unique_ptr<ldb::Process> Attach(int argc, const char** argv)
	{
		if (argc == 3 && argv[1] == std::string_view{ "-p" })
		{
			pid_t pid = std::atoi(argv[2]);
			return ldb::Process::Attach(pid);
		}
		else
		{
			const char* programPath = argv[1];
			auto proc = ldb::Process::Launch(programPath);
			fmt::print("Launched process with PID {}\n", proc->Pid());
			return proc;
		}
	}
}

void MainLoop(std::unique_ptr<ldb::Process>& process)
{
	// handle user input command
	char* line = nullptr;
	while ((line = readline("ldb> ")) != nullptr)
	{
		std::string lineString;
		if (line == std::string_view{ "" })
		{
			free(line);
			// empty line: re-run the last command if it exists
			if (history_length > 0)
			{
				lineString = history_list()[history_length - 1]->line;
			}
		}
		else
		{
			lineString = line;
			add_history(line);
			free(line);
		}
		if (!lineString.empty())
		{
			// string converts to string_view because of the conversion function [std::string::operator std::string_view()]
			try
			{
				HandleCommand(process, lineString);
			}
			catch (const ldb::Error& err)
			{
				std::cout << err.what() << '\n';
			}
		}
	}
}

int main(int argc, const char** argv)
{
	if (argc == 1)
	{
		std::cerr << "No arguments given\n";
		return -1;
	}
	
	try
	{
		auto process = Attach(argc, argv);
		MainLoop(process);
	}
	catch (const ldb::Error& err)
	{
		std::cout << err.what() << '\n';
	}
	
}