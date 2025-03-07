#pragma once

#include <libldb/elf.hpp>
#include <libldb/process.hpp>
#include <memory>

namespace ldb {
class Target {
 public:
  Target() = delete;
  Target(const Target&) = delete;
  Target& operator=(const Target&) = delete;

  static std::unique_ptr<Target> Launch(
      std::filesystem::path path,
      std::optional<int> stdout_replacement = std::nullopt);

  static std::unique_ptr<Target> Attach(pid_t pid);

  Process& process() { return *process_; }
  const Process& process() const { return *process_; }

  Elf& elf() { return *elf_; }
  const Elf& elf() const { return *elf_; }

 private:
  Target(std::unique_ptr<Process> process, std::unique_ptr<Elf> elf)
      : process_{std::move(process)}, elf_{std::move(elf)} {}

  std::unique_ptr<Process> process_;
  std::unique_ptr<Elf> elf_;
};
}  // namespace ldb