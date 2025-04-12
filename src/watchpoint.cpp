#include <libldb/watchpoint.hpp>
#include <libldb/process.hpp>
#include <libldb/error.hpp>

namespace
{
	auto GetNextId()
	{
		static ldb::Watchpoint::IdType id = 0;
		return ++id;
	}
}

namespace ldb
{
	Watchpoint::Watchpoint(Process& proc, VirtAddr addr, StoppointMode mode, std::size_t size)
		: process{ &proc }
		, address{ addr }
		, isEnabled{ false }
		, mode{ mode }
		, size{ size }
	{
		if ((addr.Addr() & (size - 1)) != 0)
		{
			Error::Send("Watchpoint must be align to sizes");
		}
		id = GetNextId();
	}

	void Watchpoint::Enable()
	{
		if (isEnabled) return;
		hardwareRegisterIndex = process->SetWatchpoint(id, address, mode, size);
		isEnabled = true;
	}

	void Watchpoint::Disable()
	{
		if (!isEnabled) return;
		process->ClearHardwareStoppoint(hardwareRegisterIndex);
		isEnabled = false;
	}
}