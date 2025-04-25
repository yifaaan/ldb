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
  explicit Disassembler(Process& proc) : process(&proc) {}

  std::vector<Instruction> Disassemble(std::size_t nInstructions,
                                       std::optional<VirtAddr> address = {});

 private:
  Process* process;
};
}  // namespace ldb