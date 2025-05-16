#pragma once

#include <cstddef>
#include <cstring>

#include "libldb/types.hpp"

namespace ldb
{
    /// @brief 从字节数组转换为指定类型
    /// @tparam To 目标类型
    /// @param bytes 字节数组
    /// @return 目标类型
    template <typename To>
    To from_bytes(const std::byte* bytes)
    {
        To ret{};
        std::memcpy(&ret, bytes, sizeof(To));
        return ret;
    }

    /// @brief 将指定类型转换为字节数组
    /// @tparam From 源类型
    /// @param from 源类型
    /// @return 字节数组
    template <typename From>
    std::byte* as_bytes(From& from)
    {
        return reinterpret_cast<std::byte*>(&from);
    }

    /// @brief 将指定类型转换为字节数组
    /// @tparam From 源类型
    /// @param from 源类型
    /// @return 字节数组
    template <typename From>
    const std::byte* as_bytes(const From& from)
    {
        return reinterpret_cast<const std::byte*>(&from);
    }

    /// @brief 将指定类型转换为 byte128
    /// @tparam From 源类型
    /// @param from 源类型
    /// @return byte128
    template <typename From>
    byte128 to_byte128(From from)
    {
        byte128 ret{};
        std::memcpy(&ret, &from, sizeof(From));
        return ret;
    }

    /// @brief 将指定类型转换为 byte64
    /// @tparam From 源类型
    /// @param from 源类型
    /// @return byte64
    template <typename From>
    byte64 to_byte64(From from)
    {
        byte64 ret{};
        std::memcpy(&ret, &from, sizeof(From));
        return ret;
    }

} // namespace ldb
