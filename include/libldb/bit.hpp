#pragma once

#include <cstddef>
#include <cstring>
#include <string_view>
#include <vector>

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

    /// @brief 将字节数组转换为 std::string_view
    /// @param bytes 字节数组
    /// @param n_bytes 字节数组大小
    /// @return std::string_view
    inline std::string_view to_string_view(const std::byte* bytes, std::size_t n_bytes)
    {
        return {reinterpret_cast<const char*>(bytes), n_bytes};
    }

    /// @brief 将字节数组转换为 std::string_view
    /// @param bytes 字节数组
    /// @return std::string_view
    inline std::string_view to_string_view(const std::vector<std::byte>& bytes)
    {
        return to_string_view(bytes.data(), bytes.size());
    }
} // namespace ldb
