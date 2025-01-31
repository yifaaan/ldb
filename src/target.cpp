#include <libldb/target.hpp>
#include <libldb/types.hpp>

namespace
{
    std::unique_ptr<ldb::Elf> CreateLoadedElf(const ldb::Process& process, const std::filesystem::path& path)
    {
        // read auxv to get the load address of entry point
        auto auxv = process.GetAuxv();
        auto elf = std::make_unique<ldb::Elf>(path);
        // calculate load bias: real virtual address of load address  -  file address in the elf file
        elf->NotifyLoaded(ldb::VirtAddr{ auxv[AT_ENTRY] - elf->GetHeader().e_entry });
        return elf;
    }
}

std::unique_ptr<ldb::Target> ldb::Target::Launch(std::filesystem::path path, bool debug, std::optional<int> stdoutReplacement)
{
    auto process = Process::Launch(path, true, stdoutReplacement);
    auto elf = CreateLoadedElf(*process, path);
    return std::unique_ptr<Target>(new Target(std::move(process), std::move(elf)));
}

std::unique_ptr<ldb::Target> ldb::Target::Attach(pid_t pid)
{
    auto elfPath = std::filesystem::path("/proc") / std::to_string(pid) / "exe";
    auto process = Process::Attach(pid);
    auto elf = CreateLoadedElf(*process, elfPath);
    return std::unique_ptr<Target>(new Target(std::move(process), std::move(elf)));
}