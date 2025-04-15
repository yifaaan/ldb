#pragma once


#include <memory>

#include <libldb/elf.hpp>
#include <libldb/process.hpp>

namespace ldb
{
    class Target
    {
    public:
        Target() = delete;
        Target(const Target&) = delete;
        Target& operator=(const Target&) = delete;
        Target(Target&&) = delete;
        Target& operator=(Target&&) = delete;

        static std::unique_ptr<Target> Launch(std::filesystem::path path, std::optional<int> stdoutReplacement = std::nullopt);

        static std::unique_ptr<Target> Attach(pid_t pid);

        Process& GetProcess() { return *process; }
        const Process& GetProcess() const { return *process; }
        
        Elf& GetElf() { return *elf; }
        const Elf& GetElf() const { return *elf; }
    private:
        Target(std::unique_ptr<Process> _process, std::unique_ptr<Elf> _elf)
            : process(std::move(_process))
            , elf(std::move(_elf))
        { }

        std::unique_ptr<Process> process;
        std::unique_ptr<Elf> elf;
    };
}