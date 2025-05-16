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

        bool is_enabled() const
        {
            return is_enabled_;
        }
        virt_addr address() const
        {
            return addr_;
        }

        bool at_address(virt_addr addr) const
        {
            return addr_ == addr;
        }

        bool in_range(virt_addr low, virt_addr high) const
        {
            return low <= addr_ && addr_ < high;
        }

    private:
        friend process;
        breakpoint_site(process& proc, virt_addr address);

        process* process_;
        virt_addr addr_;
        bool is_enabled_;
        std::byte saved_data_;
        id_type id_;
    };
} // namespace ldb
