#pragma once

#include <algorithm>
#include <libldb/error.hpp>
#include <libldb/types.hpp>
#include <memory>
#include <ranges>
#include <vector>

namespace ldb
{
    /// @brief 暂停点的集合
    /// @tparam Stoppoint 断点类型:breakpoint_site、source-level breakpoint和watchpoint
    template <typename Stoppoint, bool Owing = true>
    class stoppoint_collection
    {
    public:
        using pointer_type = std::conditional_t<Owing, std::unique_ptr<Stoppoint>, Stoppoint*>;
        /// @brief 添加暂停点
        /// @param bs 暂停点
        /// @return 暂停点
        Stoppoint& push(pointer_type bs);

        /// @brief 是否包含指定 ID 的暂停点
        /// @param id 暂停点 ID
        /// @return 是否包含
        bool contains_id(Stoppoint::id_type id) const;

        /// @brief 是否包含指定地址的暂停点
        bool contains_address(virt_addr address) const;

        /// @brief 指定地址是否启用
        /// @param address 地址
        /// @return 是否启用
        bool enabled_stoppoint_at_address(virt_addr address) const;

        /// @brief 获取指定 ID 的暂停点
        /// @param id 暂停点 ID
        /// @return 暂停点
        Stoppoint& get_by_id(Stoppoint::id_type id);

        /// @brief 获取指定 ID 的暂停点
        /// @param id 暂停点 ID
        /// @return 暂停点
        const Stoppoint& get_by_id(Stoppoint::id_type id) const;

        /// @brief 获取指定地址的暂停点
        /// @param address 地址
        /// @return 暂停点
        Stoppoint& get_by_address(virt_addr address);
        const Stoppoint& get_by_address(virt_addr address) const;

        /// @brief 删除指定 ID 的暂停点
        /// @param id 暂停点 ID
        void remove_by_id(Stoppoint::id_type id);

        /// @brief 删除指定地址的暂停点
        /// @param address 地址
        void remove_by_address(virt_addr address);

        /// @brief 遍历所有暂停点
        /// @tparam F 函数类型
        /// @param f 函数
        template <typename F>
        void for_each(F f);

        /// @brief 遍历所有暂停点
        /// @tparam F 函数类型
        /// @param f 函数
        template <typename F>
        void for_each(F f) const;

        std::size_t size() const
        {
            return stoppoints_.size();
        }
        bool empty() const
        {
            return stoppoints_.empty();
        }

        /// @brief 获取指定区域内的暂停点
        /// @param low 低地址
        /// @param high 高地址
        /// @return 暂停点
        std::vector<Stoppoint*> get_in_region(virt_addr low, virt_addr high) const;

    private:
        using points_t = std::vector<pointer_type>;

        /// @brief 查找指定 ID 的暂停点
        /// @param id 暂停点 ID
        /// @return 暂停点的迭代器
        points_t::iterator find_by_id(Stoppoint::id_type id);

        /// @brief 查找指定 ID 的暂停点
        /// @param id 暂停点 ID
        /// @return 暂停点的迭代器
        points_t::const_iterator find_by_id(Stoppoint::id_type id) const;

        /// @brief 查找指定地址的暂停点
        /// @param address 地址
        /// @return 暂停点的迭代器
        points_t::iterator find_by_address(virt_addr address);

        /// @brief 查找指定地址的暂停点
        /// @param address 地址
        /// @return 暂停点的迭代器
        points_t::const_iterator find_by_address(virt_addr address) const;

        points_t stoppoints_;
    };

    template <typename Stoppoint, bool Owing>
    Stoppoint& stoppoint_collection<Stoppoint, Owing>::push(pointer_type bs)
    {
        stoppoints_.push_back(std::move(bs));
        return *stoppoints_.back();
    }

    template <typename Stoppoint, bool Owing>
    auto stoppoint_collection<Stoppoint, Owing>::find_by_id(Stoppoint::id_type id) -> points_t::iterator
    {
        return std::ranges::find_if(stoppoints_,
                                    [id](const auto& bs)
                                    {
                                        return bs->id() == id;
                                    });
    }

    template <typename Stoppoint, bool Owing>
    auto stoppoint_collection<Stoppoint, Owing>::find_by_id(Stoppoint::id_type id) const -> points_t::const_iterator
    {
        return const_cast<stoppoint_collection*>(this)->find_by_id(id);
    }

    template <typename Stoppoint, bool Owing>
    auto stoppoint_collection<Stoppoint, Owing>::find_by_address(virt_addr address) -> points_t::iterator
    {
        return std::ranges::find_if(stoppoints_,
                                    [address](const auto& bs)
                                    {
                                        return bs->at_address(address);
                                    });
    }

    template <typename Stoppoint, bool Owing>
    auto stoppoint_collection<Stoppoint, Owing>::find_by_address(virt_addr address) const -> points_t::const_iterator
    {
        return const_cast<stoppoint_collection*>(this)->find_by_address(address);
    }

    template <typename Stoppoint, bool Owing>
    bool stoppoint_collection<Stoppoint, Owing>::contains_id(Stoppoint::id_type id) const
    {
        return find_by_id(id) != stoppoints_.end();
    }

    template <typename Stoppoint, bool Owing>
    bool stoppoint_collection<Stoppoint, Owing>::contains_address(virt_addr address) const
    {
        return find_by_address(address) != stoppoints_.end();
    }

    template <typename Stoppoint, bool Owing>
    bool stoppoint_collection<Stoppoint, Owing>::enabled_stoppoint_at_address(virt_addr address) const
    {
        return contains_address(address) && get_by_address(address).is_enabled();
    }

    template <typename Stoppoint, bool Owing>
    Stoppoint& stoppoint_collection<Stoppoint, Owing>::get_by_id(Stoppoint::id_type id)
    {
        if (auto it = find_by_id(id); it != stoppoints_.end())
        {
            return **it;
        }
        error::send("Invalid stoppoint id: " + std::to_string(id));
    }

    template <typename Stoppoint, bool Owing>
    const Stoppoint& stoppoint_collection<Stoppoint, Owing>::get_by_id(Stoppoint::id_type id) const
    {
        return const_cast<stoppoint_collection*>(this)->get_by_id(id);
    }

    template <typename Stoppoint, bool Owing>
    Stoppoint& stoppoint_collection<Stoppoint, Owing>::get_by_address(virt_addr address)
    {
        if (auto it = find_by_address(address); it != stoppoints_.end())
        {
            return **it;
        }
        error::send("Stoppoint with given address not found: " + std::to_string(address.addr()));
    }

    template <typename Stoppoint, bool Owing>
    const Stoppoint& stoppoint_collection<Stoppoint, Owing>::get_by_address(virt_addr address) const
    {
        return const_cast<stoppoint_collection*>(this)->get_by_address(address);
    }

    template <typename Stoppoint, bool Owing>
    void stoppoint_collection<Stoppoint, Owing>::remove_by_id(Stoppoint::id_type id)
    {
        auto it = find_by_id(id);
        (**it).disable();
        stoppoints_.erase(it);
    }

    template <typename Stoppoint, bool Owing>
    void stoppoint_collection<Stoppoint, Owing>::remove_by_address(virt_addr address)
    {
        auto it = find_by_address(address);
        (**it).disable();
        stoppoints_.erase(it);
    }

    template <typename Stoppoint, bool Owing>
    template <typename F>
    void stoppoint_collection<Stoppoint, Owing>::for_each(F f)
    {
        for (auto& point : stoppoints_)
        {
            f(*point);
        }
    }

    template <typename Stoppoint, bool Owing>
    template <typename F>
    void stoppoint_collection<Stoppoint, Owing>::for_each(F f) const
    {
        for (const auto& point : stoppoints_)
        {
            f(*point);
        }
    }

    template <typename Stoppoint, bool Owing>
    std::vector<Stoppoint*> stoppoint_collection<Stoppoint, Owing>::get_in_region(virt_addr low, virt_addr high) const
    {
        std::vector<Stoppoint*> ret;
        for (auto& site : stoppoints_)
        {
            if (site->in_range(low, high))
            {
                if constexpr (Owing)
                {
                    ret.push_back(site.get());
                }
                else
                {
                    ret.push_back(site);
                }
            }
        }
        return ret;
    }
} // namespace ldb
