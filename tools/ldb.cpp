
#include <unistd.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <iostream>
#include <string>
#include <vector>

#include <readline/readline.h>
#include <readline/history.h>

namespace
{
	std::vector<std::string_view> Split(std::string_view sv, char delim)
	{
		std::vector<std::string_view> splits;
		std::size_t index;
		while (true)
		{
			auto delimIndex = sv.find(delim);
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

	void Resume(pid_t pid)
	{
		if (ptrace(PTRACE_CONT, pid, nullptr, nullptr) < 0)
		{
			std::cerr << "Couldn't continue\n";
			std::exit(-1);
		}
	}


	void WaitOnSignal(pid_t pid)
	{
		int waitStatus;
		int options = 0;
		// wait the child to be stopped before main
		if (waitpid(pid, &waitStatus, options) < 0)
		{
			std::perror("waitpid failed");
			std::exit(-1);
		}
	}

	void HandleCommand(pid_t pid, std::string_view line)
	{
		auto args = Split(line, ' ');
		auto command = args[0];

		if (IsPrefix(command, "continue"))
		{
			Resume(pid);
			WaitOnSignal(pid);
		}
		else
		{
			std::cerr << "Unknown command\n";
		}
	}

	pid_t Attach(int argc, const char** argv)
	{
		pid_t pid = 0;
		if (argc == 3 && argv[1] == std::string_view{ "-p" })
		{
			if (pid = std::atoi(argv[2]); pid <= 0)
			{
				std::cerr << "Invalid pid\n";
				return -1;
			}
			if (ptrace(PTRACE_ATTACH, pid, /*addr=*/nullptr, /*data=*/nullptr) < 0)
			{
				std::perror("Could not attach to process");
				return -1;
			}
		}
		else
		{
			const char* programPath = argv[1];
			if ((pid = fork()) < 0)
			{
				std::perror("fork failed");
				return -1;
			}
			if (pid == 0)
			{
				if (ptrace(PTRACE_TRACEME, 0, nullptr, nullptr) < 0)
				{
					std::perror("Tracing failed");
					return -1;
				}
				// because of `PTRACE_TRACEME`, the child will stop before executing its main function
				if (execlp(programPath, programPath, nullptr) < 0)
				{
					std::perror("Exec failed");
					return -1;
				}
			}
		}
		return pid;
	}
}

int main(int argc, const char** argv)
{
	if (argc == 1)
	{
		std::cerr << "No arguments given\n";
		return -1;
	}
	pid_t pid = Attach(argc, argv);

	int waitStatus;
	int options = 0;
	// wait the child to be stopped before main
	if (waitpid(pid, &waitStatus, options) < 0)
	{
		std::perror("waitpid failed");
	}
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
			HandleCommand(pid, lineString);
		}
	}
}