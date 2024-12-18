#include <libldb/breakpoint_site.hpp>
#include <libldb/error.hpp>
#include <libldb/process.hpp>
#include <sys/ptrace.h>

namespace
{
    auto get_next_id()
    {
        static ldb::breakpoint_site::id_type id = 0;
        return ++id;
    }

} // namespace

ldb::breakpoint_site::breakpoint_site(process& proc, virt_addr address)
    : process_{&proc}, address_{address}, is_enabled_{false}, saved_data_{}
{
    id_ = get_next_id();
}

void ldb::breakpoint_site::enable()
{
    if (is_enabled_)
    {
        return;
    }

    errno = 0;
    std::uint64_t data = ptrace(PTRACE_PEEKDATA, process_->pid(), address_, nullptr);
    if (errno != 0)
    {
        ldb::error::send_errno("Enabling breakpoint site failed");
    }
    // we need only the first 8 bits
    saved_data_ = static_cast<std::byte>(data & 0xff);

    std::uint64_t int3 = 0xcc;
    // replace the first 8 bits with int3
    std::uint64_t data_with_int3 = ((data & ~0xff) | int3);

    if (ptrace(PTRACE_POKEDATA, process_->pid(), address_, data_with_int3) < 0)
    {
        ldb::error::send_errno("Enabling breakpoint site failed");
    }
    is_enabled_ = true;
}
