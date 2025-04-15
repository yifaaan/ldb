#pragma once

#include <cstring>
#include <vector>
#include <string_view>
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

    inline std::string_view ToStringView(const std::byte* data, std::size_t size)
    {
        return { reinterpret_cast<const char*>(data), size };
    }

    inline std::string_view ToStringView(const std::vector<std::byte>& data)
    {
        return ToStringView(data.data(), data.size());
    }


}