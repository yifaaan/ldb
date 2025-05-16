#pragma once

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <libldb/error.hpp>
#include <optional>
#include <string_view>

namespace ldb
{
    /// @brief 将字符串转换为整数
    /// @tparam T 整数类型
    /// @param sv 字符串
    /// @param base 基数
    /// @return 整数
    template <typename T>
    std::optional<T> to_integral(std::string_view sv, int base = 10)
    {
        auto begin = sv.begin();
        if (base == 16 && sv.size() > 1 && begin[0] == '0' && begin[1] == 'x')
        {
            begin += 2;
        }
        T ret;
        auto result = std::from_chars(begin, sv.end(), ret, base);
        if (result.ptr != sv.end())
        {
            return std::nullopt;
        }
        return ret;
    }

    /// @brief 将字符串转换为浮点数
    /// @tparam T 浮点数类型
    /// @param sv 字符串
    /// @return 浮点数
    template <typename T>
    std::optional<T> to_float(std::string_view sv)
    {
        T ret;
        auto result = std::from_chars(sv.begin(), sv.end(), ret);
        if (result.ptr != sv.end())
        {
            return std::nullopt;
        }
        return ret;
    }

    /// @brief 解析向量
    /// @tparam N 向量大小
    /// @param text 字符串
    /// @return 字节向量
    template <std::size_t N>
    auto parse_vector(std::string_view text)
    {
        auto invalid = []
        {
            ldb::error::send("Invalid vector format");
        };
        std::array<std::byte, N> bytes;
        const char* c = text.data();
        if (*c++ != '[')
        {
            invalid();
        }
        for (auto i = 0; i < N - 1; i++)
        {
            bytes[i] = to_integral<std::byte>({c, 4}, 16).value();
            c += 4;
            if (*c++ != ',')
            {
                invalid();
            }
        }
        bytes[N - 1] = to_integral<std::byte>({c, 4}, 16).value();
        c += 4;
        if (*c++ != ']')
        {
            invalid();
        }
        if (c != text.end())
        {
            invalid();
        }
        return bytes;
    }

    /// @brief 将字符串转换为字节
    /// @param sv 字符串
    /// @param base 基数
    /// @return 字节
    template <>
    inline std::optional<std::byte> to_integral(std::string_view sv, int base)
    {
        auto uint8 = to_integral<std::uint8_t>(sv, base);
        if (uint8)
        {
            return static_cast<std::byte>(*uint8);
        }
        return std::nullopt;
    }
} // namespace ldb
