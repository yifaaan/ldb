#include <format>

#include <libldb/target.hpp>

namespace
{
    std::unique_ptr<ldb::Elf> CreateLoadedElf(const ldb::Process& process, const std::filesystem::path& path)
    {
        auto auxv = process.GetAuxv();
        auto obj = std::make_unique<ldb::Elf>(path);
        // set load bias
        obj->NotifyLoaded(ldb::VirtAddr{auxv[AT_ENTRY] - obj->GetHeader().e_entry});
        return obj;
    }
}

namespace ldb
{
    std::unique_ptr<Target> Target::Launch(std::filesystem::path path, std::optional<int> stdoutReplacement)
    {
        auto process = Process::Launch(path, true, stdoutReplacement);
        auto obj = CreateLoadedElf(*process, path);
        return std::unique_ptr<Target>(new Target(std::move(process), std::move(obj)));
    }

    std::unique_ptr<Target> Target::Attach(pid_t pid)
    {
        auto path = std::format("/proc/{}/exe", pid);
        auto process = Process::Attach(pid);
        auto obj = CreateLoadedElf(*process, path);
        return std::unique_ptr<Target>(new Target(std::move(process), std::move(obj)));
    }
}