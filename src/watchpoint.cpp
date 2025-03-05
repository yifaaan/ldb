#include <libldb/process.hpp>
#include <libldb/watchpoint.hpp>
#include <utility>

namespace {
// Get the next ID for the watchpoint.
auto GetNextId() {
  static ldb::Watchpoint::IdType id = 0;
  return ++id;
}
}  // namespace

ldb::Watchpoint::Watchpoint(Process& process, VirtAddr address,
                            StoppointMode mode, std::size_t size)
    : process_{&process},
      address_{address},
      mode_{mode},
      size_{size},
      is_enabled_{false} {
  if ((address.addr() & (size - 1)) != 0) {
    Error::Send("Watchpoint address must be aligned to size");
  }
  id_ = GetNextId();
  UpdateData();
}

void ldb::Watchpoint::Enable() {
  if (is_enabled_) return;

  hardware_register_index_ =
      process_->SetWatchpoint(id_, address_, mode_, size_);
  is_enabled_ = true;
}

void ldb::Watchpoint::Disable() {
  if (!is_enabled_) return;

  process_->ClearHardwareStoppoint(hardware_register_index_);
  is_enabled_ = false;
}

void ldb::Watchpoint::UpdateData() {
  std::uint64_t new_data = 0;
  auto read = process_->ReadMemory(address_, size_);
  memcpy(&new_data, read.data(), size_);
  previous_data_ = std::exchange(data_, new_data);
}