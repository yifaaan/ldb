#include <sys/ptrace.h>

#include <libldb/process.hpp>
#include <libldb/error.hpp>
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
	BreakpointSite::BreakpointSite(Process& proc, VirtAddr addr, bool _isHardware, bool _isInternal)
		: process(&proc)
		, address(addr)
		, isEnabled(false)
		, isHardware(_isHardware)
		, isInternal(_isInternal)
	{
		id = isInternal ? -1 : GetNextId();
	}

	void BreakpointSite::Enable()
	{
		if (isEnabled) return;

		if (isHardware)
		{
			hardwareRegisterIndex = process->SetHardwareBreakpoint(id, address);
		}
		else
		{
			errno = 0;
			std::uint64_t data = ptrace(PTRACE_PEEKDATA, process->Pid(), address, nullptr);
			if (errno != 0)
			{
				Error::SendErrno("Enabling breakpoint site failed");
			}
			savedData = static_cast<std::byte>(data & 0xff);
			std::uint64_t int3 = 0xcc;
			std::uint64_t dataWithInt3 = (data & ~0xff) | int3;
			if (ptrace(PTRACE_POKEDATA, process->Pid(), address, dataWithInt3) < 0)
			{
				Error::SendErrno("Enabling breakpoint site failed");
			}
		}
		isEnabled = true;
	}

	void BreakpointSite::Disable()
	{
		if (!isEnabled) return;

		if (isHardware)
		{
			process->ClearHardwareStoppoint(hardwareRegisterIndex);
			hardwareRegisterIndex = -1;
		}
		else
		{
			errno = 0;
			std::uint64_t data = ptrace(PTRACE_PEEKDATA, process->Pid(), address, nullptr);
			if (errno != 0)
			{
				Error::SendErrno("Disabling breakpoint site failed");
			}
			auto restoredData = (data & ~0xff) | static_cast<std::uint8_t>(savedData);
			if (ptrace(PTRACE_POKEDATA, process->Pid(), address, restoredData) < 0)
			{
				Error::SendErrno("Disabling breakpoint site failed");
			}
		}
		isEnabled = false;
	}
}