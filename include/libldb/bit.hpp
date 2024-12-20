#ifndef LDB_BIT_HPP
#define LDB_BIT_HPP

#include <cstddef>
#include <cstring>
#include <libldb/types.hpp>
#include <string_view>
#include <vector>

namespace ldb
{

    /// Constructs an object of the given type.
    template<typename To>
    To from_bytes(const std::byte* bytes)
    {
        To ret;
        std::memcpy(&ret, bytes, sizeof(To));
        return ret;
    }
    /// Casts its argument into a pointer to std::bytes.
    template<typename From>
    std::byte* as_bytes(From& from)
    {
        return reinterpret_cast<std::byte*>(&from);
    }

    template<typename From>
    const std::byte* as_bytes(const From& from)
    {
        return reinterpret_cast<const std::byte*>(&from);
    }

    template<typename From>
    byte128 to_byte128(From src)
    {
        // Initialize all elements to 0 rather than leaving them uninitialized.
        byte128 ret{};
        std::memcpy(&ret, &src, sizeof(From));
        return ret;
    }

    template<typename From>
    byte64 to_byte64(From src)
    {
        byte64 ret{};
        std::memcpy(&ret, &src, sizeof(From));
        return ret;
    }

    inline std::string_view to_string_view(const std::byte* data, std::size_t size)
    {
        return {reinterpret_cast<const char*>(data), size};
    }

    inline std::string_view to_string_view(const std::vector<std::byte>& data)
    {
        return to_string_view(data.data(), data.size());
    }
} // namespace ldb
#endif
