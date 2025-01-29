#include <cassert>

#include <libldb/types.hpp>
#include <libldb/elf.hpp>

ldb::VirtAddr ldb::FileAddr::ToVirtAddr() const
{
    assert(elf && "ToVirtAddr called on null address");
    auto sectionHeader = elf->GetSectionContainingAddress(*this);
    if (!sectionHeader) return {};
    return VirtAddr{ addr + elf->LoadBias().Addr() };
}

ldb::FileAddr ldb::VirtAddr::ToFileAddr(const Elf& elf) const
{
    auto sectonHeader = elf.GetSectionContainingAddress(*this);
    if (!sectonHeader) return {};
    return FileAddr{ elf, addr - elf.LoadBias().Addr() };
}