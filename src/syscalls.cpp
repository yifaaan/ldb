#include <libldb/error.hpp>
#include <libldb/syscalls.hpp>
#include <unordered_map>

namespace
{
    const std::unordered_map<std::string_view, int> g_syscall_name_map{
#define DEFINE_SYSCALL(name, id) {#name, id},
#include "libldb/detail/syscalls.inc"
#undef DEFINE_SYSCALL
    };
} // namespace

std::string_view ldb::syscall_id_to_name(int id)
{
    switch (id)
    {
#define DEFINE_SYSCALL(name, id)                                                                                                                               \
    case id:                                                                                                                                                   \
        return #name;
#include "libldb/detail/syscalls.inc"
#undef DEFINE_SYSCALL
    default:
        ldb::error::send("No such syscall");
    }
}

int ldb::syscall_name_to_id(std::string_view name)
{
    if (!g_syscall_name_map.contains(name))
    {
        ldb::error::send("No such syscall");
    }
    return g_syscall_name_map.at(name);
}
