#pragma once

#include <vector>
#include <algorithm>
#include <memory>

#include <libldb/types.hpp>
#include <libldb/error.hpp>

namespace ldb
{
	
	/// <summary>
	/// collection of Stoppoints
	/// </summary>
	/// <typeparam name="Stoppoint">physical breakpoint sites, source-level breakpoints, and watchpoints</typeparam>
	template <typename Stoppoint>
	class StoppointCollection
	{
	public:
		auto Push(std::unique_ptr<Stoppoint> bs) -> Stoppoint&;

		auto ContainsId(Stoppoint::IdType id) const;
		auto ContainsAddress(VirtAddr address) const;
		auto EnabledStoppointAtAddress(VirtAddr address) const;

		auto& GetById(Stoppoint::IdType id);
		const auto& GetById(Stoppoint::IdType id) const;
		auto& GetByAddress(VirtAddr address);
		const auto& GetByAddress(VirtAddr address) const;

		std::vector<Stoppoint*> GetInRegion(VirtAddr low, VirtAddr high) const;

		void RemoveById(Stoppoint::IdType id);
		void RemoveByAddress(VirtAddr address);

		void ForEach(auto f);
		void ForEach(auto f) const;

		auto Size() const { return stoppoints.size(); }

		auto Empty() const { return stoppoints.empty(); }

	private:
		using PointsType = std::vector<std::unique_ptr<Stoppoint>>;

		auto FindById(Stoppoint::IdType id);
		auto FindById(Stoppoint::IdType id) const;

		auto FindByAddress(VirtAddr address);
		auto FindByAddress(VirtAddr address) const;

		PointsType stoppoints;
	};

	template <typename Stoppoint>
	auto StoppointCollection<Stoppoint>::Push(std::unique_ptr<Stoppoint> bs) -> Stoppoint&
	{
		stoppoints.push_back(std::move(bs));
		return *stoppoints.back();
	}

	template <typename Stoppoint>
	auto StoppointCollection<Stoppoint>::FindById(Stoppoint::IdType id)
	{
		return std::ranges::find_if(stoppoints, [id](const auto& point) { return point->Id() == id; });
	}

	template <typename Stoppoint>
	auto StoppointCollection<Stoppoint>::FindById(Stoppoint::IdType id) const
	{
		return std::ranges::find_if(stoppoints, [id](const auto& point) { return point->Id() == id; });
	}

	template <typename Stoppoint>
	auto StoppointCollection<Stoppoint>::FindByAddress(VirtAddr address)
	{
		return std::ranges::find_if(stoppoints, [address](const auto& point) { return point->Address() == address; });
	}

	template <typename Stoppoint>
	auto StoppointCollection<Stoppoint>::FindByAddress(VirtAddr address) const
	{
		return std::ranges::find_if(stoppoints, [address](const auto& point) { return point->Address() == address; });
	}

	template <typename Stoppoint>
	auto StoppointCollection<Stoppoint>::ContainsId(Stoppoint::IdType id) const
	{
		return FindById(id) != std::end(stoppoints);
	}

	template <typename Stoppoint>
	auto StoppointCollection<Stoppoint>::ContainsAddress(VirtAddr address) const
	{
		return FindByAddress(address) != std::end(stoppoints);
	}

	template <typename Stoppoint>
	auto StoppointCollection<Stoppoint>::EnabledStoppointAtAddress(VirtAddr address) const
	{
		return ContainsAddress(address) && GetByAddress(address).IsEnabled();
	}

	template <typename Stoppoint>
	auto& StoppointCollection<Stoppoint>::GetById(Stoppoint::IdType id)
	{
		if (auto it = FindById(id); it != std::end(stoppoints))
		{
			return **it;
		}
		Error::Send("Invalid stoppoint id");
	}

	template <typename Stoppoint>
	const auto& StoppointCollection<Stoppoint>::GetById(Stoppoint::IdType id) const
	{
		if (auto it = FindById(id); it != std::end(stoppoints))
		{
			return **it;
		}
		Error::Send("Invalid stoppoint id");
	}

	template <typename Stoppoint>
	auto& StoppointCollection<Stoppoint>::GetByAddress(VirtAddr address)
	{
		if (auto it = FindByAddress(address); it != std::end(stoppoints))
		{
			return **it;
		}
		Error::Send("Stoppoint with given address not found");
	}

	template <typename Stoppoint>
	const auto& StoppointCollection<Stoppoint>::GetByAddress(VirtAddr address) const
	{
		if (auto it = FindByAddress(address); it != std::end(stoppoints))
		{
			return **it;
		}
		Error::Send("Stoppoint with given address not found");
	}

	template <typename Stoppoint>
	void StoppointCollection<Stoppoint>::RemoveById(Stoppoint::IdType id)
	{
		auto it = FindById(id);
		(**it).Disable();
		stoppoints.erase(it);
	}

	template <typename Stoppoint>
	void StoppointCollection<Stoppoint>::RemoveByAddress(VirtAddr address)
	{
		auto it = FindByAddress(address);
		(**it).Disable();
		stoppoints.erase(it);
	}

	template <typename Stoppoint>
	std::vector<Stoppoint*> StoppointCollection<Stoppoint>::GetInRegion(VirtAddr low, VirtAddr high) const
	{
		std::vector<Stoppoint*> ret;
		for (auto& site : stoppoints)
		{
			if (site->InRange(low, high))
			{
				ret.push_back(&*site);
			}
		}
		return ret;
	}


	template <typename Stoppoint>
	void StoppointCollection<Stoppoint>::ForEach(auto f)
	{
		for (auto& stoppoint : stoppoints)
		{
			f(*stoppoint);
		}
	}

	template <typename Stoppoint>
	void StoppointCollection<Stoppoint>::ForEach(auto f) const
	{
		for (const auto& stoppoint : stoppoints)
		{
			f(*stoppoint);
		}
	}
}