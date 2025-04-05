#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>

#include <iostream>
#include <string>
#include <vector>
#include <format>

#include <readline/readline.h>
#include <readline/history.h>

#include <libldb/process.hpp>
#include <libldb/error.hpp>

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


	void PrintStopReason(const ldb::Process& process, ldb::StopReason reason)
	{
		std::cout << std::format("Process {} ", process.Pid());

		switch (reason.reason)
		{
		case ldb::ProcessState::exited:
			std::cout << std::format("exited with status {}\n", static_cast<int>(reason.info));
			break;
		case ldb::ProcessState::terminated:
			std::cout << std::format("terminated with signal {}\n", sigabbrev_np(reason.info));
			break;
		case ldb::ProcessState::stopped:
			std::cout << std::format("stopped with signal {}\n", sigabbrev_np(reason.info));
			break;
		}

	}

	void PrintHelp(const std::vector<std::string_view>& args)
	{
		if (args.size() == 1)
		{
			std::cerr << R"(Available commands:
continue	- Resume the process
register	- Commands for operating on registers
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
		else
		{
			std::cerr << "No help available on that\n";
		}
	}

	void HandleCommand(std::unique_ptr<ldb::Process>& process, std::string_view line)
	{
		auto args = Split(line, ' ');
		auto command = args[0];

		if (IsPrefix(command, "continue"))
		{
			process->Resume();
			auto reason = process->WaitOnSignal();
			PrintStopReason(*process, reason);
		}
		else if (IsPrefix(command, "help"))
		{
			PrintHelp(args);
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
			return ldb::Process::Launch(programPath);
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