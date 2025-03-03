#pragma once

#include <algorithm>
#include <libldb/error.hpp>
#include <libldb/types.hpp>
#include <memory>
#include <vector>

namespace ldb {
template <typename Stoppoint>
class StoppointCollection {
 public:
  // Add a stoppoint to the collection.
  Stoppoint& Push(std::unique_ptr<Stoppoint> bs) {
    stoppoints_.push_back(std::move(bs));
    return *stoppoints_.back();
  }

  // Check if the collection contains a stoppoint with the given ID.
  bool ContainsId(Stoppoint::IdType id) const;

  // Check if the collection contains a stoppoint at the given address.
  bool ContainsAddress(VirtAddr address) const;

  // Check if there is an enabled stoppoint at the given address.
  bool EnabledStoppointAtAddress(VirtAddr address) const;

  // Get the stoppoint with the given ID.
  Stoppoint& GetById(Stoppoint::IdType id);
  // Get the stoppoint with the given ID.
  const Stoppoint& GetById(Stoppoint::IdType id) const;

  // Get the stoppoint at the given address.
  Stoppoint& GetByAddress(VirtAddr address);
  // Get the stoppoint at the given address.
  const Stoppoint& GetByAddress(VirtAddr address) const;

  // Remove the stoppoint with the given ID.
  void RemoveById(Stoppoint::IdType id);

  // Remove the stoppoint at the given address.
  void RemoveByAddress(VirtAddr address);

  template <typename F>
  void ForEach(F f);
  template <typename F>
  void ForEach(F f) const;

  // Get the number of stoppoints in the collection.
  std::size_t Size() const { return stoppoints_.size(); }

  // Check if the collection is empty.
  bool Empty() const { return stoppoints_.empty(); }

 private:
  using PointsType = std::vector<std::unique_ptr<Stoppoint>>;
  PointsType::iterator FindById(Stoppoint::IdType id);
  PointsType::const_iterator FindById(Stoppoint::IdType id) const;
  PointsType::iterator FindByAddress(VirtAddr address);
  PointsType::const_iterator FindByAddress(VirtAddr address) const;
  PointsType stoppoints_;
};

template <typename Stoppoint>
bool StoppointCollection<Stoppoint>::ContainsId(Stoppoint::IdType id) const {
  return FindById(id) != std::end(stoppoints_);
}

template <typename Stoppoint>
bool StoppointCollection<Stoppoint>::EnabledStoppointAtAddress(
    VirtAddr address) const {
  return ContainsAddress(address) && GetByAddress(address).enabled();
}

template <typename Stoppoint>
bool StoppointCollection<Stoppoint>::ContainsAddress(VirtAddr address) const {
  return FindByAddress(address) != std::end(stoppoints_);
}

template <typename Stoppoint>
Stoppoint& StoppointCollection<Stoppoint>::GetById(Stoppoint::IdType id) {
  if (auto it = FindById(id); it != std::end(stoppoints_)) {
    return **it;
  }
  Error::Send("Invalid stoppoint id");
}

template <typename Stoppoint>
const Stoppoint& StoppointCollection<Stoppoint>::GetById(
    Stoppoint::IdType id) const {
  return const_cast<StoppointCollection*>(this)->GetById(id);
}

template <typename Stoppoint>
Stoppoint& StoppointCollection<Stoppoint>::GetByAddress(VirtAddr address) {
  if (auto it = FindByAddress(address); it != std::end(stoppoints_)) {
    return **it;
  }
  Error::Send("Invalid stoppoint address");
}

template <typename Stoppoint>
void StoppointCollection<Stoppoint>::RemoveById(Stoppoint::IdType id) {
  auto it = FindById(id);
  (**it).Disable();
  stoppoints_.erase(it);
}

template <typename Stoppoint>
void StoppointCollection<Stoppoint>::RemoveByAddress(VirtAddr address) {
  auto it = FindByAddress(address);
  (**it).Disable();
  stoppoints_.erase(it);
}

template <typename Stoppoint>
template <typename F>
void StoppointCollection<Stoppoint>::ForEach(F f) {
  for (auto& point : stoppoints_) {
    f(*point);
  }
}

template <typename Stoppoint>
template <typename F>
void StoppointCollection<Stoppoint>::ForEach(F f) const {
  for (const auto& point : stoppoints_) {
    f(*point);
  }
}

template <typename Stoppoint>
const Stoppoint& StoppointCollection<Stoppoint>::GetByAddress(
    VirtAddr address) const {
  return const_cast<StoppointCollection*>(this)->GetByAddress(address);
}

template <typename Stoppoint>
auto StoppointCollection<Stoppoint>::FindById(Stoppoint::IdType id)
    -> PointsType::iterator {
  return std::find_if(std::begin(stoppoints_), std::end(stoppoints_),
                      [=](const auto& point) { return point->id() == id; });
}

template <typename Stoppoint>
auto StoppointCollection<Stoppoint>::FindById(Stoppoint::IdType id) const
    -> PointsType::const_iterator {
  return const_cast<StoppointCollection*>(this)->FindById(id);
}

template <typename Stoppoint>
auto StoppointCollection<Stoppoint>::FindByAddress(VirtAddr address)
    -> PointsType::iterator {
  return std::find_if(
      std::begin(stoppoints_), std::end(stoppoints_),
      [=](const auto& point) { return point->AtAddress(address); });
}

template <typename Stoppoint>
auto StoppointCollection<Stoppoint>::FindByAddress(VirtAddr address) const
    -> PointsType::const_iterator {
  return const_cast<StoppointCollection*>(this)->FindByAddress(address);
}

}  // namespace ldb