#include <iostream>
#include <type_traits>
#include <algorithm>

#include <libldb/registers.hpp>
#include <libldb/register_info.hpp>
#include <libldb/bit.hpp>
#include <libldb/process.hpp>

namespace
{
    template<typename T>
    ldb::byte128 Widen(const ldb::RegisterInfo& info, T t)
    {
        using namespace ldb;
        if constexpr (std::is_floating_point_v<T>)
        {
            if (info.format == RegisterFormat::DoubleFloat)
            {
                return ToByte128(static_cast<double>(t));
            }
            if (info.format == RegisterFormat::LongDouble)
            {
                return ToByte128(static_cast<long double>(t));
            }
        }
        else if constexpr (std::is_unsigned_v<T>)
        {
            if (info.format == RegisterFormat::UInt)
            {
                switch (info.size)
                {
                    case 2: return ToByte128(static_cast<std::int16_t>(t));
                    case 4: return ToByte128(static_cast<std::int32_t>(t));
                    case 8: return ToByte128(static_cast<std::int64_t>(t));
                }
            }
        }
        return ToByte128(t);
    }
}

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
        if (sizeof(v) <= info.size)
        {
            auto wide = Widen(info, v);
            auto valBytes = AsBytes(wide);
            std::copy(valBytes, valBytes + info.size, bytes + info.offset);
        }
        else
        {
            std::cerr << "ldb::Registers::Write called with mismatched register and value size";
            std::terminate();
        }
    }, val);

    // PTRACE_POKEUSER and PTRACE_PEEKUSER don’t support writing and reading from the x87 area on x64
    // writing and reading all x87 registers at once
    if (info.type == RegisterType::Fpr)
    {
        proc->WriteFprs(data.i387);
    }
    else
    {
        //  PTRACE_PEEKUSER and PTRACE_POKEUSER require the addresses to align to 8 bytes
        auto alignedOffset = info.offset & ~0b111;
        // write into inferior's registers
        proc->WriteUserArea(alignedOffset, FromBytes<std::uint64_t>(bytes + alignedOffset));
    }
}