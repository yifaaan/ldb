#pragma once

#include <cstdint>
#include <libldb/target.hpp>

namespace ldb
{
    /// @brief 源码级断点
    class breakpoint
    {
    public:
        virtual ~breakpoint() = default;

        breakpoint() = delete;
        breakpoint(const breakpoint&) = delete;
        breakpoint(breakpoint&&) = default;
        breakpoint& operator=(const breakpoint&) = delete;
        breakpoint& operator=(breakpoint&&) = default;

        using id_type = std::int32_t;
        id_type id() const
        {
            return id_;
        }

        virtual void enable() = 0;
        virtual void disable() = 0;
        bool is_enabled() const
        {
            return is_enabled_;
        }

        bool is_hardware() const
        {
            return is_hardware_;
        }

        bool is_internal() const
        {
            return is_internal_;
        }

        virtual void resolve() = 0;

        stoppoint_collection<breakpoint_site, false> breakpoint_sites()
        {
            return breakpoint_sites_;
        }

        const stoppoint_collection<breakpoint_site, false>& breakpoint_sites() const
        {
            return breakpoint_sites_;
        }

        bool at_address(virt_addr addr) const
        {
            return breakpoint_sites_.contains_address(addr);
        }

        bool in_range(virt_addr low, virt_addr high) const
        {
            return !breakpoint_sites_.get_in_region(low, high).empty();
        }

    protected:
        friend class target;
        explicit breakpoint(target& tgt, bool is_hardware = false, bool is_internal = false);

        id_type id_;
        target* target_ = nullptr;
        bool is_enabled_ = false;
        bool is_hardware_ = false;
        bool is_internal_ = false;

        stoppoint_collection<breakpoint_site, false> breakpoint_sites_;
        breakpoint_site::id_type next_site_id_ = 0;
    };
} // namespace ldb
