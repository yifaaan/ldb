#pragma once

#include <cstdint>
#include <libldb/types.hpp>

namespace ldb
{
    class process;

    /// @brief 基于内存位置的物理断点
    class breakpoint_site
    {
    public:
        breakpoint_site() = delete;
        breakpoint_site(const breakpoint_site&) = delete;
        breakpoint_site(breakpoint_site&&) = delete;
        breakpoint_site& operator=(const breakpoint_site&) = delete;
        breakpoint_site& operator=(breakpoint_site&&) = delete;
        ~breakpoint_site() = default;

        using id_type = std::int32_t;
        id_type id() const
        {
            return id_;
        }

        void enable();
        void disable();

        /// 是否被启用
        bool is_enabled() const
        {
            return is_enabled_;
        }

        /// 获取断点地址
        virt_addr address() const
        {
            return addr_;
        }

        /// 是否在指定地址
        bool at_address(virt_addr addr) const
        {
            return addr_ == addr;
        }

        /// 是否在指定范围内
        bool in_range(virt_addr low, virt_addr high) const
        {
            return low <= addr_ && addr_ < high;
        }

        /// 是否是硬件断点
        bool is_hardware() const
        {
            return is_hardware_;
        }

        /// 是否是内部断点
        bool is_internal() const
        {
            return is_internal_;
        }

    private:
        friend process;
        breakpoint_site(process& proc, virt_addr address, bool is_hardware = false, bool is_internal = false);

        process* process_;
        virt_addr addr_;
        bool is_enabled_;
        /// 被int3覆盖的指令
        std::byte saved_data_;
        id_type id_;
        bool is_hardware_;
        bool is_internal_;
        /// 硬件断点在调试寄存器中的索引，dr0-dr3
        int hardware_register_index_ = -1;
    };
} // namespace ldb
