#pragma once

#include <vector>
#include <memory>
#include <algorithm>

#include <libldb/types.hpp>
#include <libldb/error.hpp>

namespace ldb
{
    template<typename Stoppoint>
    class StoppointCollection
    {
    public:
        Stoppoint& Push(std::unique_ptr<Stoppoint> bs)
        {
            stoppoints.push_back(std::move(bs));
            return *stoppoints.back();
        }

        bool ContainsId(typename Stoppoint::IdType id) const
        {
            return FindById(id) != std::end(stoppoints);
        }

        bool ContainsAddress(VirtAddr address) const
        {
            return FindByAddress(address) != std::end(stoppoints);
        }

        bool EnabledStoppointAtAddress(VirtAddr address) const
        {
            return ContainsAddress(address) and GetByAddress(address).isEnabled();
        }

        Stoppoint& GetById(typename Stoppoint::IdType id)
        {
            if (auto it = FindById(id); it == std::end(stoppoints))
            {
                Error::Send("Invalid stoppoint id");
            }
            else
            {
                return **it;
            }
        }
        const Stoppoint& GetById(typename Stoppoint::IdType id) const
        {
            return const_cast<StoppointCollection*>(this)->GetById(id);
        }

        Stoppoint& GetByAddress(VirtAddr address)
        {
            if (auto it = FindByAddress(address); it == std::end(stoppoints))
            {
                Error::Send("Invalid stoppoint address");
            }
            else
            {
                return **it;
            }
        }

        const Stoppoint& GetByAddress(VirtAddr address) const
        {
            return const_cast<StoppointCollection*>(this)->GetByAddress(address);
        }

        void RemoveById(typename Stoppoint::IdType id)
        {
            auto it = FindById(id);
            (**it).Disable();
            stoppoints.erase(it);
        }

        void RemoveByAddress(VirtAddr address)
        {
            auto it = FindByAddress(address);
            (**it).Disable();
            stoppoints.erase(it);
        }

        template<typename F>
        void ForEach(F f)
        {
            for (auto& point : stoppoints)
            {
                f(*point);
            }
        }

        template<typename F>
        void ForEach(F f) const
        {
            for (const auto& point : stoppoints)
            {
                f(*point);
            }
        }

        std::size_t Size() const { return stoppoints.size(); }
        bool Empty() const { return stoppoints.empty(); }

    private:
        using TPoints = std::vector<std::unique_ptr<Stoppoint>>;

        auto FindById(typename Stoppoint::IdType id) -> typename TPoints::iterator
        {
            return std::find_if(std::begin(stoppoints), std::end(stoppoints),[=](auto& point)
            {
                return point->Id() == id;
            });
        }

        auto FindById(typename Stoppoint::IdType id) const -> typename TPoints::const_iterator
        {
            return const_cast<StoppointCollection*>(this)->FindById(id);
        }

        auto FindByAddress(VirtAddr address) -> typename TPoints::iterator 
        {
            return std::find_if(std::begin(stoppoints), std::end(stoppoints), [=](auto& point)
            {
                return point->AtAddress(address);
            });
        }
        auto FindByAddress(VirtAddr address) const -> typename TPoints::const_iterator
        {
            return const_cast<StoppointCollection*>(this)->FindByAddress(address);
        }
        
        TPoints stoppoints;
    };

}