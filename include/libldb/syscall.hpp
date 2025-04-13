#pragma once

#include <string_view>

namespace ldb
{
	std::string_view SyscallIdToName(int id);
	int SyscallNameToId(std::string_view name);
}