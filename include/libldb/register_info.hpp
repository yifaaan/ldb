#ifndef LDB_REGISTER_INFO_HPP
#define LDB_REGISTER_INFO_HPP

#include <string_view>
namespace ldb {
enum class register_id {

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

inline constexpr const register_info g_register_infos[] = {};
} // namespace ldb

#endif