#ifndef LDB_REGISTERS_HPP
#define LDB_REGISTERS_HPP

#include "libldb/types.hpp"
#include <libldb/register_info.hpp>
#include <sys/user.h>
#include <variant>

namespace ldb {
class process;
class registers {
public:
  registers() = delete;
  registers(const registers&) = delete;
  registers& operator=(const registers&) = delete;

  using value =
      std::variant<std::uint8_t, std::uint16_t, std::uint32_t, std::uint64_t,
                   std::int8_t, std::int16_t, std::int32_t, std::int64_t, float,
                   double, long double, byte64, byte128>;
  value read(const register_info& info) const;
  void write(const register_info& info, value val);

  /// Using an ID to retrieve the register value as a specific type.
  template <typename T> T read_by_id_as(register_id id) const {
    return std::get<T>(read(register_info_by_id(id)));
  }
  /// Writing to a register given its ID.
  void write_by_id(register_id id, value val) {
    write(register_info_by_id(id), val);
  }

private:
  friend process;
  registers(process& proc) : proc_(&proc) {}
  user data_;
  process* proc_;
};
} // namespace ldb

#endif