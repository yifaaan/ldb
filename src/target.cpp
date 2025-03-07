#include <libldb/target.hpp>
#include <libldb/types.hpp>

namespace {
std::unique_ptr<ldb::Elf> CreateLoadedElf(const ldb::Process& process,
                                          const std::filesystem::path& path) {
  auto auxv = process.GetAuxv();

  auto elf = std::make_unique<ldb::Elf>(path);
  elf->NotifyLoaded(ldb::VirtAddr{auxv[AT_ENTRY]} - elf->header().e_entry);
  return elf;
}
}  // namespace

std::unique_ptr<ldb::Target> ldb::Target::Launch(
    std::filesystem::path path, std::optional<int> stdout_replacement) {
  auto process = Process::Launch(path, true, stdout_replacement);
  auto elf = CreateLoadedElf(*process, path);
  return std::unique_ptr<Target>(
      new Target{std::move(process), std::move(elf)});
}

std::unique_ptr<ldb::Target> ldb::Target::Attach(pid_t pid) {
  // The symbol link to the executable is /proc/pid/exe
  auto elf_path = std::filesystem::path{"/proc"} / std::to_string(pid) / "exe";
  auto process = Process::Attach(pid);
  auto elf = CreateLoadedElf(*process, elf_path);
  return std::unique_ptr<Target>(
      new Target{std::move(process), std::move(elf)});
}