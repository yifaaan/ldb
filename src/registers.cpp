#include <exception>
#include <iostream>
#include <libldb/bit.hpp>
#include <libldb/process.hpp>
#include <libldb/registers.hpp>
#include <libldb/types.hpp>

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
        if (sizeof(v) == info.size) {
          auto value_bytes = AsBytes(v);
          std::copy(value_bytes, value_bytes + sizeof(v), bytes + info.offset);
        } else {
          std::cerr
              << "ldb::Registers::Write called with mismatched register and "
                 "value sizes";
          std::terminate();
        }
      },
      value);

  // Write the value to the user area ptrace provides to update the register.
  process_->WriteUserArea(info.offset,
                          FromBytes<std::uint64_t>(bytes + info.offset));
}
}  // namespace ldb