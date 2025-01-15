#ifndef LDB_PARSE_HPP
#define LDB_PARSE_HPP

#include <optional>
#include <string_view>
#include <charconv>
#include <cstdint>

#include <libldb/error.hpp>

namespace ldb
{
    template<typename I>
    std::optional<I> ToIntegral(std::string_view sv, int base = 10)
    {
        auto begin = sv.begin();
        // skip 0x
        if (base == 16 and sv.size() > 1 and begin[0] == '0' and begin[1] == 'x')
        {
            begin += 2;
        }

        I ret;
        auto res = std::from_chars(begin, sv.end(), ret, base);
        if (res.ptr != sv.end())
        {
            return std::nullopt;
        }
        return ret;
    }

    template<>
    inline std::optional<std::byte> ToIntegral(std::string_view sv, int base)
    {
        auto uint8 = ToIntegral<std::uint8_t>(sv, base);
        if (uint8) return static_cast<std::byte>(*uint8);
        return std::nullopt;
    }

    template<typename F>
    std::optional<F> ToFloat(std::string_view sv)
    {
        F ret;
        auto res = std::from_chars(sv.begin(), sv.end(), ret);
        if (res.ptr != sv.end())
        {
            return std::nullopt;
        }
        return ret;
    }

    template<std::size_t N>
    auto ParseVector(std::string_view text)
    {
        auto invalid = [] { ldb::Error::Send("Invalid format"); };

        std::array<std::byte, N> bytes;
        const char* c = text.data();

        if (*c++ != '[') invalid();
        for (auto i = 0; i < N - 1; i++)
        {
            bytes[i] = ToIntegral<std::byte>({c, 4}, 16).value();
            c += 4;
            if (*c++ != ',') invalid();
        }
        bytes[N - 1] = ToIntegral<std::byte>({c, 4}, 16).value();
        c += 4;

        if (*c++ != ']' || c != text.end()) invalid();
        return bytes;
    }

    inline auto ParseVector(std::string_view text)
    {
        auto invalid = [] { ldb::Error::Send("Invalid format"); };

        std::vector<std::byte> bytes;
        const char* c = text.data();

        if (*c++ != '[') invalid();
        while (*c != ']')
        {
            auto byte = ToIntegral<std::byte>({c, 4}, 16);
            bytes.push_back(byte.value());
            c += 4;
            if (*c == ',') ++c;
            else if (*c != ']') invalid();
        }
        
        if (++c != text.end()) invalid();
        return bytes;
    }
}

#endif