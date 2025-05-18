#pragma once

#include <cstdint>
#include <libldb/types.hpp>
namespace ldb
{
    class process;

    class watchpoint
    {
    public:
        watchpoint() = delete;
        watchpoint(const watchpoint&) = delete;
        watchpoint(watchpoint&&) = delete;
        watchpoint& operator=(const watchpoint&) = delete;
        watchpoint& operator=(watchpoint&&) = delete;
        ~watchpoint() = default;

        using id_type = std::int32_t;

        /// 获取断点 ID
        id_type id() const
        {
            return id_;
        }

        void enable();
        void disable();

        /// 是否启用
        bool is_enabled() const
        {
            return is_enabled_;
        }

        /// 获取断点地址
        virt_addr address() const
        {
            return address_;
        }

        /// 获取断点模式
        stoppoint_mode mode() const
        {
            return mode_;
        }

        /// 获取断点大小
        std::size_t size() const
        {
            return size_;
        }

        /// 是否在指定地址
        bool at_address(virt_addr addr) const
        {
            return address_ == addr;
        }

        /// 是否在指定范围内
        bool in_range(virt_addr low, virt_addr high) const
        {
            return low <= address_ && address_ < high;
        }

    private:
        friend process;

        watchpoint(process& process, virt_addr address, stoppoint_mode mode, std::size_t size);

        id_type id_;
        process* process_;
        virt_addr address_;
        std::size_t size_;
        stoppoint_mode mode_;
        bool is_enabled_;
        /// 硬件断点索引: dr0-dr3
        int hardware_register_index_ = -1;
    };
} // namespace ldb
