#include <libldb/elf.hpp>
#include <libldb/types.hpp>

namespace ldb {
FileAddr VirtAddr::ToFileAddr(const Elf& elf) const {
  if (auto sc = elf.GetSectionContainingAddress(*this); sc) {
    return {elf, addr - elf.LoadBias().Addr()};
  }
  return {};
}

VirtAddr FileAddr::ToVirtAddr() const {
  assert(elf && "ToVirtAddr() called on a null address");
  if (auto sc = elf->GetSectionContainingAddress(*this); sc) {
    return VirtAddr{addr + elf->LoadBias().Addr()};
  }
  return {};
}
}  // namespace ldb