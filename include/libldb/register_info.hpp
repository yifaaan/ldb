#pragma once

#include <sys/user.h>

#include <cstddef>
#include <cstdint>
#include <libldb/error.hpp>
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

template <typename F>
const RegisterInfo& RegisterInfoBy(F f) {
  if (auto it =
          std::find_if(std::begin(RegisterInfos), std::end(RegisterInfos), f);
      it != std::end(RegisterInfos)) {
    return *it;
  }
  Error::Send("Can't find register info");
}

// Get register info by id.
inline const RegisterInfo& RegisterInfoById(RegisterId id) {
  return RegisterInfoBy([id](const auto& r) { return r.id == id; });
}

// Get register info by name.
inline const RegisterInfo& RegisterInfoByName(std::string_view name) {
  return RegisterInfoBy([name](const auto& r) { return r.name == name; });
}

// Get register info by dwarf id.
inline const RegisterInfo& RegisterInfoByDwarfId(std::int32_t dwarf_id) {
  return RegisterInfoBy(
      [dwarf_id](const auto& r) { return r.dwarf_id == dwarf_id; });
}

}  // namespace ldb