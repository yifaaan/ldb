#include <libldb/error.hpp>
#include <libldb/syscall.hpp>
#include <unordered_map>

namespace {
const std::unordered_map<std::string_view, int> syscallNameMap{
#define DEFINE_SYSCALL(name, id) {#name, id},
#include "include/syscall.inc"
#undef DEFINE_SYSCALL
};
}  // namespace

namespace ldb {
std::string_view SyscallIdToName(int id) {
  switch (id) {
#define DEFINE_SYSCALL(name, id) \
  case id:                       \
    return #name;
#include "include/syscall.inc"
#undef DEFINE_SYSCALL
    default:
      Error::Send("No such syscall");
  }
}
int SyscallNameToId(std::string_view name) {
  if (syscallNameMap.contains(name)) {
    return syscallNameMap.at(name);
  }
  Error::Send("No such syscall");
}
}  // namespace ldb