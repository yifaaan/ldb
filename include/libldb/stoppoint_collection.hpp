#ifndef LDB_STOPPOINT_COLLECTION_HPP
#define LDB_STOPPOINT_COLLECTION_HPP

#include <libldb/error.hpp>
#include <libldb/types.hpp>
#include <memory>
#include <vector>

namespace ldb
{
    template<typename Stoppoint>
    class stoppoint_collection
    {
    public:
        Stoppoint& push(std::unique_ptr<Stoppoint> stop);
        bool contains_id(typename Stoppoint::id_type id) const;
        bool contains_address(virt_addr address) const;
        bool enabled_stoppoint_at_address(virt_addr address) const;

        Stoppoint& get_by_id(typename Stoppoint::id_type id);
        const Stoppoint& get_by_id(typename Stoppoint::id_type id) const;
        Stoppoint& get_by_address(virt_addr address);
        const Stoppoint& get_by_address(virt_addr address) const;

        void remove_by_id(typename Stoppoint::id_type id);
        void remove_by_address(virt_addr address);

        template<typename F>
        void for_each(F f);
        template<typename F>
        void for_each(F f) const;

        std::size_t size() const
        {
            return stoppoints_.size();
        }
        bool empty() const
        {
            return stoppoints_.empty();
        }

        std::vector<Stoppoint*> get_in_region(virt_addr low, virt_addr high) const;

    private:
        using points_t = std::vector<std::unique_ptr<Stoppoint>>;

        typename points_t::iterator find_by_id(typename Stoppoint::id_type id);
        typename points_t::const_iterator find_by_id(typename Stoppoint::id_type id) const;
        typename points_t::iterator find_by_address(virt_addr address);
        typename points_t::const_iterator find_by_address(virt_addr address) const;

        points_t stoppoints_;
    };

    template<typename Stoppoint>
    Stoppoint& stoppoint_collection<Stoppoint>::push(std::unique_ptr<Stoppoint> bs)
    {
        stoppoints_.push_back(std::move(bs));
        return *stoppoints_.back();
    }

    template<typename Stoppoint>
    auto stoppoint_collection<Stoppoint>::find_by_id(typename Stoppoint::id_type id) -> typename points_t::iterator
    {
        return std::find_if(std::begin(stoppoints_), std::end(stoppoints_),
                            [=](auto& point) { return point->id() == id; });
    }

    template<typename Stoppoint>
    auto stoppoint_collection<Stoppoint>::find_by_id(typename Stoppoint::id_type id) const ->
        typename points_t::const_iterator
    {
        return const_cast<stoppoint_collection*>(this)->find_by_id(id);
    }

    template<typename Stoppoint>
    auto stoppoint_collection<Stoppoint>::find_by_address(virt_addr address) -> typename points_t::iterator
    {
        return std::find_if(std::begin(stoppoints_), std::end(stoppoints_),
                            [=](auto& point) { return point->at_address(address); });
    }

    template<typename Stoppoint>
    auto stoppoint_collection<Stoppoint>::find_by_address(virt_addr address) const -> typename points_t::const_iterator
    {
        return const_cast<stoppoint_collection<Stoppoint>*>(this)->find_by_address(address);
    }

    template<typename Stoppoint>
    bool stoppoint_collection<Stoppoint>::contains_id(typename Stoppoint::id_type id) const
    {
        return find_by_id(id) != std::end(stoppoints_);
    }

    template<typename Stoppoint>
    bool stoppoint_collection<Stoppoint>::contains_address(virt_addr address) const
    {
        return find_by_address(address) != std::end(stoppoints_);
    }

    template<typename Stoppoint>
    bool stoppoint_collection<Stoppoint>::enabled_stoppoint_at_address(virt_addr address) const
    {
        return contains_address(address) and get_by_address(address).is_enabled();
    }

    template<typename Stoppoint>
    Stoppoint& stoppoint_collection<Stoppoint>::get_by_id(typename Stoppoint::id_type id)
    {
        if (auto it = find_by_id(id); it != std::end(stoppoints_))
        {
            return **it;
        }
        else
        {
            ldb::error::send("Invalid stoppoint id");
        }
    }

    template<typename Stoppoint>
    const Stoppoint& stoppoint_collection<Stoppoint>::get_by_id(typename Stoppoint::id_type id) const
    {
        return const_cast<stoppoint_collection<Stoppoint>*>(this)->get_by_id(id);
    }

    template<typename Stoppoint>
    Stoppoint& stoppoint_collection<Stoppoint>::get_by_address(virt_addr address)
    {
        if (auto it = find_by_address(address); it != std::end(stoppoints_))
        {
            return **it;
        }
        else
        {
            ldb::error::send("Stoppoint with given address not found");
        }
    }

    template<typename Stoppoint>
    const Stoppoint& stoppoint_collection<Stoppoint>::get_by_address(virt_addr address) const
    {
        return const_cast<stoppoint_collection<Stoppoint>*>(this)->get_by_address(address);
    }

    template<typename Stoppoint>
    void stoppoint_collection<Stoppoint>::remove_by_id(typename Stoppoint::id_type id)
    {
        auto it = find_by_id(id);
        (**it).disable();
        stoppoints_.erase(it);
    }

    template<typename Stoppoint>
    void stoppoint_collection<Stoppoint>::remove_by_address(virt_addr address)
    {
        auto it = find_by_address(address);
        (**it).disable();
        stoppoints_.erase(it);
    }

    template<typename Stoppoint>
    template<typename F>
    void stoppoint_collection<Stoppoint>::for_each(F f)
    {
        for (auto& point : stoppoints_)
        {
            f(*point);
        }
    }

    template<typename Stoppoint>
    template<typename F>
    void stoppoint_collection<Stoppoint>::for_each(F f) const
    {
        for (const auto& point : stoppoints_)
        {
            f(*point);
        }
    }

    template<typename Stoppoint>
    std::vector<Stoppoint*> stoppoint_collection<Stoppoint>::get_in_region(virt_addr low, virt_addr high) const
    {
        std::vector<Stoppoint*> ret;
        for (auto& site : stoppoints_)
        {
            if (site->is_range(low, high))
            {
                ret.push_back(&*site);
            }
        }
        return ret;
    }

} // namespace ldb

#endif
