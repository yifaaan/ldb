#ifndef LDB_REGISTER_INFO_HPP
#define LDB_REGISTER_INFO_HPP

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <sys/user.h>

namespace ldb {
enum class register_id {
#define DEFINE_REGISTER(name, dwarf_id, size, offset, type, format) name
#include <libldb/detail/register.inc>
#undef DEFINE_REGISTER
};

enum class register_type {
  gpr,
  /// Like how eax is the 32-bit version of rax
  sub_gpr,
  fpr,
  /// Debug register
  dr
};

/// The different ways of interpreting a register.
enum class register_format { uint, double_float, long_double, vector };

struct register_info {
  register_id id;
  std::string_view name;
  std::int32_t dwarf_id;
  std::size_t size;
  std::size_t offset;
  register_type type;
  register_format format;
};

/// Every register in the system.
inline constexpr const register_info g_register_infos[] = {
#define DEFINE_REGISTER(name, dwarf_id, size, offset, type, format)            \
  {register_id::name, #name, dwarf_id, size, offset, type, format}
#include <libldb/detail/register.inc>
#undef DEFINE_REGISTER
};

/// To find a specific register info entry.
template <typename F> const register_info& register_info_by(F f) {
  if (auto it = std::find_if(std::begin(g_register_infos),
                             std::end(g_register_infos), f);
      it == std::end(g_register_infos)) {
    error::send("Can't find register info");
  } else {
    return *it;
  }
}

inline const register_info& register_info_by_id(register_id id) {
  return register_info_by([id](auto& i) { return i.id == id; });
}
inline const register_info& register_info_by_name(std::string_view name) {
  return register_info_by([name](auto& i) { return i.name == name; });
}
inline const register_info& register_info_by_dwarf(std::int32_t dwarf_id) {
  return register_info_by(
      [dwarf_id](auto& i) { return i.dwarf_id == dwarf_id; });
}
} // namespace ldb

#endif