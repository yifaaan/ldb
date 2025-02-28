#include <libldb/bit.hpp>
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
}  // namespace ldb