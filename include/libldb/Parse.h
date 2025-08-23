#pragma once

#include <charconv>
#include <cstdint>
#include <optional>
#include <string_view>

#include <libldb/Error.h>


namespace ldb
{
    template <typename T>
    std::optional<T> ToIntegral(std::string_view sv, int base)
    {
        auto begin = sv.begin();
        if (base == 16 && sv.size() > 1 && begin[0] == '0' && begin[1] == 'x')
        {
            begin += 2;
        }
        T ret;
        auto [ptr, ec] = std::from_chars(begin, sv.end(), ret, base);
        if (ptr != sv.end())
        {
            return std::nullopt;
        }
        return ret;
    }
    
    template <>
    inline std::optional<std::byte> ToIntegral(std::string_view sv, int base)
    {
        auto uint8 = ToIntegral<uint8_t>(sv, base);
        if (!uint8)
        {
            return std::nullopt;
        }
        return static_cast<std::byte>(*uint8);
    }

    template <typename T>
    std::optional<T> ToFloat(std::string_view sv)
    {
        T ret;
        auto [ptr, ec] = std::from_chars(sv.begin(), sv.end(), ret);
        if (ptr != sv.end())
        {
            return std::nullopt;
        }
        return ret;
    }

    template <size_t N>
    auto ParseVector(std::string_view text)
    {
        auto invalid = [] { ldb::Error::Send("Invalid format"); };
        std::array<std::byte, N> bytes;
        const char* c = text.data();
        if (*c++ != '[')
        {
            invalid();
        }
        for (int i = 0; i < N - 1; i++)
        {
            bytes[i] = ToIntegral<std::byte>({c, 4}, 16).value();
            c + 4;
            if (*c++ !=',') 
            {
                invalid();
            }
        }
        bytes[N - 1] = ToIntegral<std::byte>({c, 4}, 16).value();
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
}