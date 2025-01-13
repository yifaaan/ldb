#include <iostream>

#include <libldb/registers.hpp>
#include <libldb/register_info.hpp>
#include <libldb/bit.hpp>

ldb::Registers::value ldb::Registers::Read(const RegisterInfo& info) const
{
    auto bytes = AsBytes(data);

    if (info.format == RegisterFormat::UInt)
    {
        switch (info.size)
        {
            case 1: return FromBytes<std::uint8_t>(bytes + info.offset);
            case 2: return FromBytes<std::uint16_t>(bytes + info.offset);
            case 4: return FromBytes<std::uint32_t>(bytes + info.offset);
            case 8: return FromBytes<std::uint64_t>(bytes + info.offset);
            default: ldb::Error::Send("Unexpected register size");
        }
    }
    else if (info.format == RegisterFormat::DoubleFloat)
    {
        return FromBytes<double>(bytes + info.offset);
    }
    else if (info.format == RegisterFormat::LongDouble)
    {
        return FromBytes<long double>(bytes + info.offset);
    }
    else if (info.format == RegisterFormat::Vector and info.size == 8)
    {
        return FromBytes<byte64>(bytes + info.offset);
    }
    else
    {
        return FromBytes<byte128>(bytes + info.offset);
    }
}

void ldb::Registers::Write(const RegisterInfo& info, value val)
{
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

    // write into inferior's registers
    proc->WriteUserArea(info.offset, FromBytes<std::uint64_t>(bytes + info.offset));
}