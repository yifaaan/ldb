#include <iostream>

#include <libldb/Error.h>
#include <libldb/Registers.h>
#include <libldb/bit.h>
#include <libldb/Process.h>

namespace
{
    template <typename T>
    ldb::Byte128 Widen(const ldb::RegisterInfo& info, T t)
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
        else if constexpr (std::is_signed_v<T>)
        {
            if (info.format == RegisterFormat::UInt)
            {
                switch (info.size)
                {
                    case 2: return ToByte128(static_cast<uint16_t>(t));
                    case 4: return ToByte128(static_cast<uint32_t>(t));
                    case 8: return ToByte128(static_cast<uint64_t>(t));
                }
            }
        }
        return ToByte128(t);
    }
}

namespace ldb
{
    Registers::Value Registers::Read(const RegisterInfo& info) const
    {
        auto bytes = AsBytes(data);

        if (info.format == RegisterFormat::UInt)
        {
            switch (info.size)
            {
            case 1:
                return FromBytes<uint8_t>(bytes + info.offset);
            case 2:
                return FromBytes<uint16_t>(bytes + info.offset);
            case 4:
                return FromBytes<uint32_t>(bytes + info.offset);
            case 8:
                return FromBytes<uint64_t>(bytes + info.offset);
            default:
                Error::Send("Unexpected register size");
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
        else if (info.format == RegisterFormat::Vector && info.size == 8)
        {
            return FromBytes<Byte64>(bytes + info.offset);
        }
        else
        {
            return FromBytes<Byte128>(bytes + info.offset);
        }
    }

    void Registers::Write(const RegisterInfo& info, Value value)
    {
        auto bytes = AsBytes(data);
        // Write the value to the (bytes + info.offset)
        std::visit([&](auto& v)
        {
            if (sizeof(v) <= info.size)
            {
                auto wide = Widen(info, v);
                auto val_bytes = AsBytes(wide);
                std::copy(val_bytes, val_bytes + info.size, bytes + info.offset);
            }
            else
            {
                std::cerr << "ldb::Registers::Write called with mismatched register and value size";
                std::terminate();
            }
        }, value);

        if (info.type == RegisterType::Fpr)
        {
            process->WriteFPRs(data.i387);
        }
        else
        {
            auto alignedOffset = info.offset  & 0b111;
            process->WriteUserArea(alignedOffset, FromBytes<uint64_t>(bytes + info.offset));
        }
    }
} // namespace ldb