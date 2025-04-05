
#include <libldb/breakpoint_site.hpp>

namespace
{
	auto GetNextId()
	{
		static ldb::BreakpointSite::IdType id = 0;
		return ++id;
	}
}

namespace ldb
{
	BreakpointSite::BreakpointSite(Process& proc, VirtAddr addr)
			: process(&proc), address(addr), isEnabled(false)
	{
		id = GetNextId();
	}
}