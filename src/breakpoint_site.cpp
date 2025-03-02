#include <libldb/breakpoint_site.hpp>

namespace {
// Get the next ID for the breakpoint site.
auto GetNextId() {
  static ldb::BreakpointSite::IdType id = 0;
  return ++id;
}
}  // namespace

ldb::BreakpointSite::BreakpointSite(Process& process, VirtAddr address)
    : process_{&process}, address_{address}, is_enabled_{false}, saved_data_{} {
  id_ = GetNextId();
}