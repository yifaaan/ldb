#pragma once

#include <sys/user.h>

#include <libldb/register_info.hpp>
#include <libldb/types.hpp>
#include <variant>

namespace ldb {
class Process;
class Registers {
 public:
  Registers() = default;
  Registers(const Registers&) = default;
  Registers& operator=(const Registers&) = default;
  Registers(Registers&&) = default;
  Registers& operator=(Registers&&) = default;
  ~Registers() = default;

  using Value = std::variant<std::uint8_t, std::uint16_t, std::uint32_t, std::uint64_t, std::int8_t, std::int16_t,
                             std::int32_t, std::int64_t, float, double, long double, Byte64, Byte128>;

  Value Read(const RegisterInfo& info) const;

  void Write(const RegisterInfo& info, Value val, bool commit = true);

  template <typename T>
  T ReadByIdAs(RegisterId id) const {
    return std::get<T>(Read(RegisterInfoById(id)));
  }

  void WriteById(RegisterId id, Value val, bool commit = true) { Write(RegisterInfoById(id), val, commit); }

  bool IsUndefined(RegisterId id) const;
  void Undefine(RegisterId id);

  VirtAddr cfa() const { return cfa_; }
  void SetCfa(VirtAddr addr) { cfa_ = addr; }

  /// 将寄存器写回进程
  void Flush();

 private:
  friend Process;
  explicit Registers(Process& proc) : process_{&proc} {}

  user data_;
  Process* process_;
  VirtAddr cfa_;
  std::vector<std::size_t> undefined_registers_;
};
}  // namespace ldb