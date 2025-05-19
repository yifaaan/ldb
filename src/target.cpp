#include <libldb/elf.hpp>
#include <libldb/target.hpp>
#include <libldb/types.hpp>

namespace
{
    std::unique_ptr<ldb::elf> create_loaded_elf(const ldb::process& process, const std::filesystem::path& path)
    {
        auto auxv = process.get_auxv();
        auto obj = std::make_unique<ldb::elf>(path);

        std::uint64_t actual_entry_point = auxv.at(AT_ENTRY);
        ldb::virt_addr load_bias{actual_entry_point - obj->header().e_entry};
        // 设置加载偏移量
        obj->notify_loaded(load_bias);
        return obj;
    }
} // namespace

std::unique_ptr<ldb::target> ldb::target::launch(std::filesystem::path path, std::optional<int> stdout_replacement)
{
    auto proc = process::launch(path, true, stdout_replacement);
    if (!proc)
    {
        return nullptr;
    }
    auto obj = create_loaded_elf(*proc, path);
    if (!obj)
    {
        return nullptr;
    }
    return std::unique_ptr<target>(new target(std::move(proc), std::move(obj)));
}

std::unique_ptr<ldb::target> ldb::target::attach(pid_t pid)
{
    // 获取进程的 ELF 路径
    auto elf_path = std::filesystem::path{"/proc"} / std::to_string(pid) / "exe";
    auto proc = process::attach(pid);
    if (!proc)
    {
        return nullptr;
    }
    auto obj = create_loaded_elf(*proc, elf_path);
    if (!obj)
    {
        return nullptr;
    }
    return std::unique_ptr<target>(new target(std::move(proc), std::move(obj)));
}
