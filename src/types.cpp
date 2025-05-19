#include <cassert>
#include <libldb/elf.hpp>
#include <libldb/types.hpp>

ldb::virt_addr ldb::file_addr::to_virt_addr() const
{
    assert(elf_ && "to_virt_addr called on null address");
    auto header = elf_->get_section_containing_address(*this);
    if (!header)
    {
        // 如果找不到包含该地址的 section header，则返回无效的虚拟地址
        return {};
    }
    return virt_addr{elf_->load_bias() + addr_};
}

ldb::file_addr ldb::virt_addr::to_file_addr(const elf& obj) const
{
    auto header = obj.get_section_containing_address(*this);
    if (!header)
    {
        return {};
    }
    return file_addr{obj, addr_ - obj.load_bias().addr()};
}
