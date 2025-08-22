#pragma once

#include <vector>
#include <memory>

#include <libldb/Types.h>
#include <libldb/Error.h>

namespace ldb
{
    template <typename Stoppoint>
    class StoppointCollection
    {
    public:
        Stoppoint& Push(std::unique_ptr<Stoppoint> bs);

        bool ContainsId(typename Stoppoint::IdType id) const;
        bool ContainsAddress(VirtAddr address) const;
        bool EnabledStoppointAtAddress(VirtAddr address) const;
        Stoppoint& GetById(typename Stoppoint::IdType id);
        const Stoppoint& GetById(typename Stoppoint::IdType id) const;
        Stoppoint& GetByAddress(VirtAddr address);
        const Stoppoint& GetByAddress(VirtAddr address) const;
        void RemoveById(typename Stoppoint::IdType id);
        void RemoveByAddress(VirtAddr address);

        template <typename F>
        void ForEach(F f);
        template <typename F>
        void ForEach(F f) const;
        size_t Size() const { return stoppoints.size(); }
        bool Empty() const { return stoppoints.empty(); }

        private:
        using PointsType = std::vector<std::unique_ptr<Stoppoint>>;
        PointsType::iterator FindById(typename Stoppoint::IdType id);
        PointsType::const_iterator FindById(typename Stoppoint::IdType id) const;
        PointsType::iterator FindByAddress(VirtAddr address);
        PointsType::const_iterator FindByAddress(VirtAddr address) const;

        PointsType stoppoints;
    };

    template <typename Stoppoint>
    Stoppoint& StoppointCollection<Stoppoint>::Push(std::unique_ptr<Stoppoint> bs)
    {
        stoppoints.push_back(std::move(bs));
        return *stoppoints.back();
    }

    template <typename Stoppoint>
    auto StoppointCollection<Stoppoint>::FindById(typename Stoppoint::IdType id) -> PointsType::iterator
    {
        return std::ranges::find_if(stoppoints, [=](const auto& p) { return p->Id() == id; });
    }

    template <typename Stoppoint>
    auto StoppointCollection<Stoppoint>::FindById(typename Stoppoint::IdType id) const -> PointsType::const_iterator
    {
        return const_cast<StoppointCollection*>(this)->FindById(id);
    }

    template <typename Stoppoint>
    auto StoppointCollection<Stoppoint>::FindByAddress(VirtAddr address) -> PointsType::iterator
    {
        return std::ranges::find_if(stoppoints, [=](const auto& p) { return p->Address() == address; });
    }

    template <typename Stoppoint>
    auto StoppointCollection<Stoppoint>::FindByAddress(VirtAddr address) const -> PointsType::const_iterator
    {
        return const_cast<StoppointCollection*>(this)->FindByAddress(address);
    }

    template <typename Stoppoint>
    bool StoppointCollection<Stoppoint>::ContainsId(typename Stoppoint::IdType id) const
    {
        return FindById(id) != stoppoints.end();
    }

    template <typename Stoppoint>
    bool StoppointCollection<Stoppoint>::ContainsAddress(VirtAddr address) const
    {
        return FindByAddress(address) != stoppoints.end();
    }
    
    template <typename Stoppoint>
    bool StoppointCollection<Stoppoint>::EnabledStoppointAtAddress(VirtAddr address) const
    {
        return ContainsAddress(address) && GetByAddress(address).IsEnabled();
    }
    
    template <typename Stoppoint>
    Stoppoint& StoppointCollection<Stoppoint>::GetById(typename Stoppoint::IdType id)
    {
        if (auto it = FindById(id); it != stoppoints.end())
        {
            return **it;
        }
        Error::Send("Invalid stoppoint id");
    }

    template <typename Stoppoint>
    const Stoppoint& StoppointCollection<Stoppoint>::GetById(typename Stoppoint::IdType id) const
    {
        return const_cast<StoppointCollection*>(this)->GetById(id);
    }
    
    template <typename Stoppoint>
    Stoppoint& StoppointCollection<Stoppoint>::GetByAddress(VirtAddr address)
    {
        if (auto it = FindByAddress(address); it != stoppoints.end())
        {
            return **it;
        }
        Error::Send("Stoppoint with given address not found");
    }
    
    template <typename Stoppoint>
    const Stoppoint& StoppointCollection<Stoppoint>::GetByAddress(VirtAddr address) const
    {
        return const_cast<StoppointCollection*>(this)->GetByAddress(address);
    }
    
    template <typename Stoppoint>
    void StoppointCollection<Stoppoint>::RemoveById(typename Stoppoint::IdType id)
    {
        if (auto it = FindById(id); it != stoppoints.end())
        {
            (**it).Disable();
            stoppoints.erase(it);
            return;
        }
        Error::Send("Stoppoint with given id not found");
    }

    template <typename Stoppoint>
    void StoppointCollection<Stoppoint>::RemoveByAddress(VirtAddr address)
    {
        if (auto it = FindByAddress(address); it != stoppoints.end())
        {
            (**it).Disable();
            stoppoints.erase(it);
            return;
        }
        Error::Send("Stoppoint with given address not found");
    }

    template <typename Stoppoint>
    template <typename F>
    void StoppointCollection<Stoppoint>::ForEach(F f)
    {
        for (auto& p : stoppoints)
        {
            f(*p);
        }
    }

    template <typename Stoppoint>
    template <typename F>
    void StoppointCollection<Stoppoint>::ForEach(F f) const
    {
        for (const auto& p : stoppoints)
        {
            f(*p);
        }
    }
}