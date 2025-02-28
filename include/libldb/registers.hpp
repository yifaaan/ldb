#pragma once

#include <sys/user.h>

#include <libldb/process.hpp>
#include <libldb/register_info.hpp>
#include <libldb/types.hpp>
#include <variant>

namespace ldb {
class Registers {
 public:
  Registers() = delete;
  Registers(const Registers&) = delete;
  Registers& operator=(const Registers&) = delete;

  using Value =
      std::variant<std::uint8_t, std::uint16_t, std::uint32_t, std::uint64_t,
                   std::int8_t, std::int16_t, std::int32_t, std::int64_t, float,
                   double, long double, Byte64, Byte128>;

  // Read the value of the register identified by the given info.
  Value Read(const RegisterInfo& info) const;

  // Write the value to the register identified by the given info.
  void Write(const RegisterInfo& info, Value value);

  // Read the value of the register identified by the given id as the given
  // type.
  template <typename T>
  T ReadByIdAs(RegisterId id) const {
    return std::get<T>(Read(RegisterInfoById(id)));
  }

  // Write the value to the register identified by the given id.
  void WriteById(RegisterId id, Value value) {
    Write(RegisterInfoById(id), value);
  }

 private:
  friend Process;
  // Only Process can create Registers.
  Registers(Process& process) : process_{&process} {}

  // The registers are stored in the user struct.
  user data_;
  Process* process_;
};
}  // namespace ldb