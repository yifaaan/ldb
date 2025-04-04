#pragma once

#include "libldb/types.hpp"
#include <libldb/bit.hpp>
#include <libldb/registers.hpp>

namespace ldb
{
    Registers::Value Registers::Read(const RegisterInfo& info) const
    {
        auto bytes = AsBytes(data);

        if (info.format == RegisterFormat::uint)
        {
            switch (info.size)
            {
            case 1: return FromBytes<std::uint8_t>(bytes + info.offset);
            case 2: return FromBytes<std::uint16_t>(bytes + info.offset);
            case 4: return FromBytes<std::uint32_t>(bytes + info.offset);
            case 8: return FromBytes<std::uint64_t>(bytes + info.offset);
            default: Error::Send("Unexpected register size");
            }
        }
        else if (info.format == RegisterFormat::doubleFloat)
        {
            return FromBytes<double>(bytes + info.offset);
        }
        else if (info.format == RegisterFormat::longDouble)
        {
            return FromBytes<long double>(bytes + info.offset);
        }
        else if (info.format == RegisterFormat::vector && info.size == 8)
        {
            return FromBytes<Byte64>(bytes + info.offset);
        }
        else
        {
            return FromBytes<Byte128>(bytes + info.offset);
        }
    }
}