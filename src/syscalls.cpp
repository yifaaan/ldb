#pragma once

#include <libldb/error.hpp>
#include <libldb/syscalls.hpp>
#include <string_view>
#include <unordered_map>

namespace {
const std::unordered_map<std::string_view, int> SyscallNameMap = {
#define DEFINE_SYSCALL(name, id) {#name, id},
#include "libldb/detail/syscalls.inc"
#undef DEFINE_SYSCALL
};

}  // namespace

std::string_view ldb::SyscallIdToName(int id) {
  switch (id) {
#define DEFINE_SYSCALL(name, id) \
  case id:                       \
    return #name;
#include "libldb/detail/syscalls.inc"
#undef DEFINE_SYSCALL
    default:
      ldb::Error::Send("No such syscall");
  }
}

int ldb::SyscallNameToId(std::string_view name) {
  if (SyscallNameMap.contains(name) != 1) {
    ldb::Error::Send("No such syscall");
  }
  return SyscallNameMap.at(name);
}