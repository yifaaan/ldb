#pragma once

#include <libldb/process.hpp>
#include <optional>

namespace ldb {
class Disassembler {
 private:
  struct Instruction {
    VirtAddr address;
    std::string text;
  };

 public:
  explicit Disassembler(Process& process) : process_{&process} {}

  // Disassemble n_instructions instructions starting from address.
  // If address is not provided, disassemble instructions from the current
  // instruction pointer.
  std::vector<Instruction> Disassemble(
      std::size_t n_instructions,
      std::optional<VirtAddr> address = std::nullopt);

 private:
  Process* process_;
};
}  // namespace ldb