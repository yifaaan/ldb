#pragma once

#include <algorithm>
#include <libldb/error.hpp>
#include <libldb/types.hpp>
#include <memory>
#include <vector>

namespace ldb {

/// <summary>
/// collection of Stoppoints
/// </summary>
/// <typeparam name="Stoppoint">physical breakpoint sites, source-level
/// breakpoints, and watchpoints</typeparam>
template <typename Stoppoint, bool Owning = true>
class StoppointCollection {
 public:
  using PointerType =
      std::conditional_t<Owning, std::unique_ptr<Stoppoint>, Stoppoint*>;

  auto Push(PointerType bs) -> Stoppoint&;

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
  using PointsType = std::vector<PointerType>;

  auto FindById(Stoppoint::IdType id);
  auto FindById(Stoppoint::IdType id) const;

  auto FindByAddress(VirtAddr address);
  auto FindByAddress(VirtAddr address) const;

  PointsType stoppoints;
};

template <typename Stoppoint, bool Owning>
auto StoppointCollection<Stoppoint, Owning>::Push(PointerType bs)
    -> Stoppoint& {
  stoppoints.push_back(std::move(bs));
  return *stoppoints.back();
}

template <typename Stoppoint, bool Owning>
auto StoppointCollection<Stoppoint, Owning>::FindById(Stoppoint::IdType id) {
  return std::ranges::find_if(
      stoppoints, [id](const auto& point) { return point->Id() == id; });
}

template <typename Stoppoint, bool Owning>
auto StoppointCollection<Stoppoint, Owning>::FindById(
    Stoppoint::IdType id) const {
  return std::ranges::find_if(
      stoppoints, [id](const auto& point) { return point->Id() == id; });
}

template <typename Stoppoint, bool Owning>
auto StoppointCollection<Stoppoint, Owning>::FindByAddress(VirtAddr address) {
  return std::ranges::find_if(stoppoints, [address](const auto& point) {
    return point->Address() == address;
  });
}

template <typename Stoppoint, bool Owning>
auto StoppointCollection<Stoppoint, Owning>::FindByAddress(
    VirtAddr address) const {
  return std::ranges::find_if(stoppoints, [address](const auto& point) {
    return point->Address() == address;
  });
}

template <typename Stoppoint, bool Owning>
auto StoppointCollection<Stoppoint, Owning>::ContainsId(
    Stoppoint::IdType id) const {
  return FindById(id) != std::end(stoppoints);
}

template <typename Stoppoint, bool Owning>
auto StoppointCollection<Stoppoint, Owning>::ContainsAddress(
    VirtAddr address) const {
  return FindByAddress(address) != std::end(stoppoints);
}

template <typename Stoppoint, bool Owning>
auto StoppointCollection<Stoppoint, Owning>::EnabledStoppointAtAddress(
    VirtAddr address) const {
  return ContainsAddress(address) && GetByAddress(address).IsEnabled();
}

template <typename Stoppoint, bool Owning>
auto& StoppointCollection<Stoppoint, Owning>::GetById(Stoppoint::IdType id) {
  if (auto it = FindById(id); it != std::end(stoppoints)) {
    return **it;
  }
  Error::Send("Invalid stoppoint id");
}

template <typename Stoppoint, bool Owning>
const auto& StoppointCollection<Stoppoint, Owning>::GetById(
    Stoppoint::IdType id) const {
  if (auto it = FindById(id); it != std::end(stoppoints)) {
    return **it;
  }
  Error::Send("Invalid stoppoint id");
}

template <typename Stoppoint, bool Owning>
auto& StoppointCollection<Stoppoint, Owning>::GetByAddress(VirtAddr address) {
  if (auto it = FindByAddress(address); it != std::end(stoppoints)) {
    return **it;
  }
  Error::Send("Stoppoint with given address not found");
}

template <typename Stoppoint, bool Owning>
const auto& StoppointCollection<Stoppoint, Owning>::GetByAddress(
    VirtAddr address) const {
  if (auto it = FindByAddress(address); it != std::end(stoppoints)) {
    return **it;
  }
  Error::Send("Stoppoint with given address not found");
}

template <typename Stoppoint, bool Owning>
void StoppointCollection<Stoppoint, Owning>::RemoveById(Stoppoint::IdType id) {
  auto it = FindById(id);
  (**it).Disable();
  stoppoints.erase(it);
}

template <typename Stoppoint, bool Owning>
void StoppointCollection<Stoppoint, Owning>::RemoveByAddress(VirtAddr address) {
  auto it = FindByAddress(address);
  (**it).Disable();
  stoppoints.erase(it);
}

template <typename Stoppoint, bool Owning>
std::vector<Stoppoint*> StoppointCollection<Stoppoint, Owning>::GetInRegion(
    VirtAddr low, VirtAddr high) const {
  std::vector<Stoppoint*> ret;
  for (auto& site : stoppoints) {
    if (site->InRange(low, high)) {
      ret.push_back(&*site);
    }
  }
  return ret;
}

template <typename Stoppoint, bool Owning>
void StoppointCollection<Stoppoint, Owning>::ForEach(auto f) {
  for (auto& stoppoint : stoppoints) {
    f(*stoppoint);
  }
}

template <typename Stoppoint, bool Owning>
void StoppointCollection<Stoppoint, Owning>::ForEach(auto f) const {
  for (const auto& stoppoint : stoppoints) {
    f(*stoppoint);
  }
}
}  // namespace ldb