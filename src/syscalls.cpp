#include <unordered_map>


#include <fmt/format.h>

#include <libldb/syscalls.hpp>
#include <libldb/error.hpp>


namespace
{
    const std::unordered_map<std::string_view, int> GSyscallNameMap = 
    {
        #define DEFINE_SYSCALL(name,id) { #name, id },
        #include "libldb/detail/syscalls.inc"
        #undef DEFINE_SYSCALL
    };
}

std::string_view ldb::SyscallIdToName(int id)
{
    switch (id)
    {
        #define DEFINE_SYSCALL(name,id) case id: return #name;
        #include "libldb/detail/syscalls.inc"
        #undef DEFINE_SYSCALL
    default: ldb::Error::Send("SyscallIdToName: No such syscall");
    }
}

int ldb::SyscallNameToId(std::string_view name)
{
    if (GSyscallNameMap.contains(name))
    {
        return GSyscallNameMap.at(name);
    }
    ldb::Error::Send("SyscallNameToId: No such syscall");
}