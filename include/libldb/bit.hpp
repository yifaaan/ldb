#pragma once

#include <cstring>

#include <libldb/types.hpp>

namespace ldb
{
    template <typename To>
    To FromBytes(const std::byte* bytes)
    {
        To ret;
        std::memcpy(&ret, bytes, sizeof(To));
        return ret;
    }

    std::byte* AsBytes(auto& from)
    {
        return reinterpret_cast<std::byte*>(&from);
    }

    const std::byte* AsBytes(const auto& from)
    {
        return reinterpret_cast<const std::byte*>(&from);
    }


    Byte128 ToByte128(auto src)
    {
        Byte128 ret{};
        std::memcpy(&ret, &src, sizeof(src));
        return ret;
    }

    Byte64 ToByte64(auto src)
    {
        Byte64 ret{};
        std::memcpy(&ret, &src, sizeof(src));
        return ret;
    }

}