#pragma once

#include <sys/user.h>

#include <libldb/register_info.hpp>

namespace ldb
{
	class Process;
	class Registers
	{

	private:
		friend Process;
		explicit Registers(Process& proc)
			: process{ &proc }
		{
		}

		user data;
		Process* process;
	};
}