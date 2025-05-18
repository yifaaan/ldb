#include <sys/ptrace.h>

#include <cerrno>
#include <libldb/breakpoint_site.hpp>
#include <libldb/process.hpp>

namespace
{
    auto get_next_id()
    {
        static ldb::breakpoint_site::id_type id = 0;
        return ++id;
    }
} // namespace

ldb::breakpoint_site::breakpoint_site(process& proc, virt_addr address, bool is_hardware, bool is_internal)
    : process_{&proc}
    , addr_{address}
    , is_enabled_{false}
    , saved_data_{}
    , is_hardware_{is_hardware}
    , is_internal_{is_internal}
{
    id_ = is_internal ? -1 : get_next_id();
}

void ldb::breakpoint_site::enable()
{
    if (is_enabled_)
        return;
    if (is_hardware_)
    {
        // 设置硬件断点
        hardware_register_index_ = process_->set_hardware_breakpoint(id_, addr_);
    }
    else
    {
        // 设置软件断点
        errno = 0;
        // 读取地址处的数据
        std::uint64_t data = ptrace(PTRACE_PEEKDATA, process_->pid(), addr_, nullptr);
        if (errno != 0)
        {
            error::send_errno("Enable breakpoint site failed");
        }
        // 保存地址处的数据
        saved_data_ = static_cast<std::byte>(data & 0xff);
        // 设置断点
        std::uint64_t int3 = 0xcc;
        // 将地址处的数据设置为int3
        std::uint64_t data_with_int3 = (data & ~0xff) | int3;
        // 写入地址处
        if (ptrace(PTRACE_POKEDATA, process_->pid(), addr_, data_with_int3) < 0)
        {
            error::send_errno("Enable breakpoint site failed");
        }
    }
    is_enabled_ = true;
}

void ldb::breakpoint_site::disable()
{
    if (!is_enabled_)
        return;
    if (is_hardware_)
    {
        // 清除硬件断点
        process_->clear_hardware_stoppoint(hardware_register_index_);
        // 清除硬件断点对应调试寄存器索引
        hardware_register_index_ = -1;
    }
    else
    {
        errno = 0;
        std::uint64_t data = ptrace(PTRACE_PEEKDATA, process_->pid(), addr_, nullptr);
        if (errno != 0)
        {
            error::send_errno("Disable breakpoint site failed");
        }
        auto restored_data = (data & ~0xff) | static_cast<std::uint8_t>(saved_data_);
        if (ptrace(PTRACE_POKEDATA, process_->pid(), addr_, restored_data) < 0)
        {
            error::send_errno("Disable breakpoint site failed");
        }
    }
    is_enabled_ = false;
}
