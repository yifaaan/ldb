#include <libldb/error.hpp>
#include <libldb/process.hpp>
#include <libldb/watchpoint.hpp>
#include <utility>

namespace {
auto GetNextId() {
  static ldb::Watchpoint::IdType id = 0;
  return ++id;
}
}  // namespace

namespace ldb {
Watchpoint::Watchpoint(Process& proc, VirtAddr addr, StoppointMode mode,
                       std::size_t size)
    : process{&proc}, address{addr}, isEnabled{false}, mode{mode}, size{size} {
  if ((addr.Addr() & (size - 1)) != 0) {
    Error::Send("Watchpoint must be align to sizes");
  }
  id = GetNextId();
  UpdateData();
}

void Watchpoint::Enable() {
  if (isEnabled) return;
  hardwareRegisterIndex = process->SetWatchpoint(id, address, mode, size);
  isEnabled = true;
}

void Watchpoint::Disable() {
  if (!isEnabled) return;
  process->ClearHardwareStoppoint(hardwareRegisterIndex);
  isEnabled = false;
}

void Watchpoint::UpdateData() {
  std::uint64_t newData = 0;
  auto read = process->ReadMemory(address, size);
  std::memcpy(&newData, read.data(), size);
  previousData = std::exchange(data, newData);
}
}  // namespace ldb