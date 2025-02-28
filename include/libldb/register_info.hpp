#pragma once

#include <sys/user.h>

#include <cstddef>
#include <cstdint>
#include <string_view>
namespace ldb {
enum class RegisterId {
#define DEFINE_REGISTER(name, dwarf_id, size, offset, type, format) name
#include <libldb/detail/registers.inc>
#undef DEFINE_REGISTER
};

enum class RegisterType {
  Gpr,
  SubGpr,
  Fpr,
  Dr,
};

enum class RegisterFormat {
  Uint,
  DoubleFloat,
  LongDouble,
  Vector,
};

struct RegisterInfo {
  RegisterId id;
  std::string_view name;
  std::int32_t dwarf_id;
  std::size_t size;
  // Offset in the user structure.
  std::size_t offset;
  RegisterType type;
  RegisterFormat format;
};

inline constexpr RegisterInfo RegisterInfos[] = {
#define DEFINE_REGISTER(name, dwarf_id, size, offset, type, format) \
  {RegisterId::name, #name, dwarf_id, size, offset, type, format}
#include <libldb/detail/registers.inc>
#undef DEFINE_REGISTER
};
}  // namespace ldb