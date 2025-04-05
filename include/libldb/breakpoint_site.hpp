#pragma once

#include <libldb/types.hpp>

namespace ldb
{
	class Process;
	class BreakpointSite
	{
	public:
		BreakpointSite() = delete;
		BreakpointSite(const BreakpointSite&) = delete;
		BreakpointSite& operator=(const BreakpointSite&) = delete;
		BreakpointSite(BreakpointSite&&) = delete;
		BreakpointSite& operator=(BreakpointSite&&) = delete;

		using IdType = std::int32_t;
		IdType Id() const { return id; }

		void Enable();
		void Disable();

		bool IsEnabled() const { return isEnabled; }

		VirtAddr Address() const { return address; }

		bool AtAddress(VirtAddr addr) const { return address == addr; }

		bool InRange(VirtAddr low, VirtAddr high) const { return low <= address && address < high; }


	private:
		BreakpointSite(Process& proc, VirtAddr addr);
		friend Process;

		IdType id;
		Process* process;
		VirtAddr address;
		bool isEnabled;
		std::byte savedData{};
	};
}