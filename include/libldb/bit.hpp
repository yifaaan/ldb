#ifndef LDB_BIT_HPP
#define LDB_BIT_HPP

#include <cstddef>
#include <cstring>

#include <libldb/types.hpp>

namespace ldb
{
    template<typename To>
    To FromBytes(const std::byte* bytes)
    {
        To ret;
        std::memcpy(&ret, bytes, sizeof(To));
        return ret;
    }

    template<typename From>
    std::byte* AsBytes(From& from)
    {
        return reinterpret_cast<std::byte*>(&from);
    }

    template<typename From>
    const std::byte* AsBytes(const From& from)
    {
        return reinterpret_cast<const std::byte*>(&from);
    }

    template<typename From>
    byte64 ToByte64(From from)
    {
        byte64 ret{};
        // std::memcpy(ret.data(), &from, sizeof(From));
        std::memcpy(&ret, &from, sizeof(From));
        return ret;
    }

    template<typename From>
    byte128 ToByte128(From from)
    {
        byte128 ret{};
        std::memcpy(&ret, &from, sizeof(From));
        return ret;
    }
}

#endif