#pragma once

#include <iostream>

#include <libldb/bit.hpp>
#include <libldb/registers.hpp>
#include <libldb/process.hpp>

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

    void Registers::Write(const RegisterInfo& info, Value val)
    {
        // update the data
        auto bytes = AsBytes(data);
        std::visit([&](auto& v)
        {
            if (sizeof(v) == info.size)
            {
                auto valBytes = AsBytes(v);
                std::copy(valBytes, valBytes + sizeof(v), bytes + info.offset);
            }
            else
            {
                std::cerr << "ldb::Registers::Write called with mismatched register and value size";
                std::terminate();
            }
        }, val);

        // write to the inferior's registers
        process->WriteUserArea(info.offset, FromBytes<std::uint64_t>(bytes + info.offset));
    }
}