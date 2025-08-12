#pragma once

#include <libldb/types.h>

#include <cstddef>
#include <cstring>

namespace ldb
{
    template <typename To>
    To FromBytes(const std::byte* bytes)
    {
        To ret;
        std::memcpy(&ret, bytes, sizeof(To));
        return ret;
    }

    template <typename From>
    std::byte* AsBytes(From& from)
    {
        return reinterpret_cast<std::byte*>(&from);
    }

    // 新增：const 重载，支持在 const 上下文中获取字节指针
    template <typename From>
    const std::byte* AsBytes(const From& from)
    {
        return reinterpret_cast<const std::byte*>(&from);
    }

    template <typename From>
    Byte128 ToByte128(From src)
    {
        Byte128 ret{};
        std::memcpy(&ret, &src, sizeof(From));
        return ret;
    }

    template <typename From>
    Byte64 ToByte64(From src)
    {
        Byte64 ret{};
        std::memcpy(&ret, &src, sizeof(From));
        return ret;
    }

} // namespace ldb