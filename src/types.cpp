#include <cassert>
#include <libldb/elf.hpp>
#include <libldb/types.hpp>

ldb::VirtAddr ldb::FileAddr::ToVirtAddr() const {
  assert(elf_ && "ToVirtAddr called on null address");
  // Get the section's header containing the file address.
  auto section_header = elf_->GetSectionHeaderContainingAddress(*this);
  if (!section_header) return {};
  return VirtAddr{addr_ + elf_->load_bias().addr()};
}

ldb::FileAddr ldb::VirtAddr::ToFileAddr(const Elf& elf) const {
  auto section_header = elf.GetSectionHeaderContainingAddress(*this);
  if (!section_header) return {};
  return FileAddr{elf, addr_ - elf.load_bias().addr()};
}