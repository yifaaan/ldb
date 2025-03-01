#include <exception>
#include <iostream>
#include <libldb/bit.hpp>
#include <libldb/process.hpp>
#include <libldb/registers.hpp>
#include <libldb/types.hpp>
#include <type_traits>

#include "libldb/register_info.hpp"

namespace {

// Widens the value to a 128-bit value to fit the register size.
// If T is unsigned integer, it is not needed to be widened.
template <typename T>
ldb::Byte128 Widen(const ldb::RegisterInfo& info, T t) {
  using namespace ldb;
  if constexpr (std::is_floating_point_v<T>) {
    if (info.format == RegisterFormat::DoubleFloat) {
      return ToByte128(static_cast<double>(t));
    }
    if (info.format == RegisterFormat::LongDouble) {
      return ToByte128(static_cast<long double>(t));
    }
  } else if constexpr (std::is_signed_v<T>) {
    if (info.format == RegisterFormat::Uint) {
      switch (info.size) {
        case 2:
          return ToByte128(static_cast<std::int16_t>(t));
        case 4:
          return ToByte128(static_cast<std::int32_t>(t));
        case 8:
          return ToByte128(static_cast<std::int64_t>(t));
      }
    }
  }
  return ToByte128(t);
}
}  // namespace

namespace ldb {
ldb::Registers::Value ldb::Registers::Read(const RegisterInfo& info) const {
  auto bytes = AsBytes(data_);

  if (info.format == RegisterFormat::Uint) {
    switch (info.size) {
      case 1:
        return FromBytes<std::uint8_t>(bytes + info.offset);
      case 2:
        return FromBytes<std::uint16_t>(bytes + info.offset);
      case 4:
        return FromBytes<std::uint32_t>(bytes + info.offset);
      case 8:
        return FromBytes<std::uint64_t>(bytes + info.offset);
      default:
        ldb::Error::Send("Unexpected register size");
    }
  } else if (info.format == RegisterFormat::DoubleFloat) {
    return FromBytes<double>(bytes + info.offset);
  } else if (info.format == RegisterFormat::LongDouble) {
    return FromBytes<long double>(bytes + info.offset);
  } else if (info.format == RegisterFormat::Vector && info.size == 8) {
    return FromBytes<Byte64>(bytes + info.offset);
  } else {
    return FromBytes<Byte128>(bytes + info.offset);
  }
}

void ldb::Registers::Write(const RegisterInfo& info, Value value) {
  auto bytes = AsBytes(data_);

  std::visit(
      [&](const auto& v) {
        if (sizeof(v) <= info.size) {
          // If the value is smaller than the register size, widen the value to
          // fit the register size.
          auto widened = Widen(info, v);
          auto value_bytes = AsBytes(widened);
          std::copy(value_bytes, value_bytes + info.size, bytes + info.offset);
        } else {
          std::cerr
              << "ldb::Registers::Write called with mismatched register and "
                 "value sizes";
          std::terminate();
        }
      },
      value);

  // If the register is a floating point register, write all the floating point
  // registers.
  if (info.type == RegisterType::Fpr) {
    process_->WriteFprs(data_.i387);
  } else {
    // Write the value to the user area ptrace provides to update the general
    // purpose register or debug register.
    // The prace function requires the offset to be aligned to 8 bytes.
    auto aligned_offset = info.offset & ~0x7;
    process_->WriteUserArea(aligned_offset,
                            FromBytes<std::uint64_t>(bytes + aligned_offset));
  }
}
}  // namespace ldb