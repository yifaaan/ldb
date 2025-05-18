#include <libldb/process.hpp>
#include <libldb/watchpoint.hpp>
#include <utility>

namespace
{
    auto get_next_id()
    {
        static ldb::watchpoint::id_type next_id = 0;
        return ++next_id;
    }
} // namespace

ldb::watchpoint::watchpoint(process& process, virt_addr address, stoppoint_mode mode, std::size_t size)
    : id_(get_next_id())
    , process_(&process)
    , address_(address)
    , size_(size)
    , mode_(mode)
    , is_enabled_(false)
{
    // watchpoint 地址必须对齐到 size
    if ((address_.addr() & (size - 1)) != 0)
    {
        throw std::invalid_argument("Watchpoint address must be aligned to size");
    }
    update_data();
}

void ldb::watchpoint::enable()
{
    if (is_enabled_)
    {
        return;
    }
    hardware_register_index_ = process_->set_watchpoint(id_, address_, mode_, size_);
    is_enabled_ = true;
}

void ldb::watchpoint::disable()
{
    if (!is_enabled_)
    {
        return;
    }
    process_->clear_hardware_stoppoint(hardware_register_index_);
    hardware_register_index_ = -1;
    is_enabled_ = false;
}

void ldb::watchpoint::update_data()
{
    std::uint64_t current_value = 0;
    // 从监视点地址读取size_字节的数据
    auto bytes_read = process_->read_memory(address_, size_);
    if (bytes_read.size() >= size_)
    {
        std::memcpy(&current_value, bytes_read.data(), size_);
    }
    else
    {
        // TODO: 读取失败
    }
    previous_data_ = std::exchange(data_, current_value);
}
