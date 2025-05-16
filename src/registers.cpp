#include <cstdint>
#include <exception>
#include <iostream>
#include <libldb/bit.hpp>
#include <libldb/process.hpp>
#include <libldb/register_info.hpp>
#include <libldb/registers.hpp>
#include <type_traits>
#include <variant>

namespace
{
    /// @brief 将数字类型转换为 byte128
    /// @tparam T 源类型
    /// @param info 寄存器信息
    /// @param t 源类型
    /// @return byte128
    template <typename T>
    ldb::byte128 widen(const ldb::register_info& info, T t)
    {
        using namespace ldb;
        if constexpr (std::is_floating_point_v<T>)
        {
            if (info.format == register_format::double_float)
            {
                return to_byte128(static_cast<double>(t));
            }
            else if (info.format == register_format::long_double)
            {
                return to_byte128(static_cast<long double>(t));
            }
        }
        else if constexpr (std::is_signed_v<T>)
        {
            if (info.format == register_format::uint)
            {
                switch (info.size)
                {
                case 2:
                    return to_byte128(static_cast<std::int16_t>(t));
                case 4:
                    return to_byte128(static_cast<std::int32_t>(t));
                case 8:
                    return to_byte128(static_cast<std::int64_t>(t));
                }
            }
        }
        return to_byte128(t);
    }
} // namespace

ldb::registers::value ldb::registers::read(const ldb::register_info& info) const
{
    auto bytes = as_bytes(data_);
    if (info.format == register_format::uint)
    {
        switch (info.size)
        {
        case 1:
            return from_bytes<std::uint8_t>(bytes + info.offset);
        case 2:
            return from_bytes<std::uint16_t>(bytes + info.offset);
        case 4:
            return from_bytes<std::uint32_t>(bytes + info.offset);
        case 8:
            return from_bytes<std::uint64_t>(bytes + info.offset);
        default:
            throw std::runtime_error("Unsupported register size");
        }
    }
    else if (info.format == register_format::double_float)
    {
        return from_bytes<double>(bytes + info.offset);
    }
    else if (info.format == register_format::long_double)
    {
        return from_bytes<long double>(bytes + info.offset);
    }
    else if (info.format == register_format::vector && info.size == 8)
    {
        return from_bytes<byte64>(bytes + info.offset);
    }
    else
    {
        return from_bytes<byte128>(bytes + info.offset);
    }
}

void ldb::registers::write(const ldb::register_info& info, ldb::registers::value v)
{
    // 获取寄存器数据地址
    auto bytes = as_bytes(data_);
    // 访问寄存器值
    std::visit(
    [&](auto& value)
    {
        if (sizeof(value) <= info.size)
        {
            // 根据目标寄存器的特性，将value正确地扩展（零扩展、符号扩展或浮点转换）为一个统一的小端字节序列 sdb::byte128
            auto wide = widen(info, value);
            auto value_bytes = as_bytes(wide);
            // 将寄存器值的字节数据写入寄存器
            std::copy(value_bytes, value_bytes + info.size, bytes + info.offset);
        }
        else
        {
            std::cerr << "ldb::registers::write called with mismatch register and value sizes\n";
            std::terminate();
        }
    },
    v);

    if (info.type == register_type::fpr)
    {
        // 一次性写入所有 GPRs
        proc_->write_fprs(data_.i387);
    }
    else
    {
        // 写入单个 GPR 或调试寄存器的值
        auto aligned_offset = info.offset & ~0b111;
        // 写入寄存器值
        proc_->write_user_area(aligned_offset, from_bytes<std::uint64_t>(bytes + aligned_offset));
    }
}
