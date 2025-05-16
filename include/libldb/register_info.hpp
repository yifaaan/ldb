#pragma once

#include <sys/user.h>

#include <algorithm>
#include <cstdint>
#include <libldb/error.hpp>
#include <string_view>

namespace ldb
{
    /// @brief 寄存器 ID
    enum class register_id
    {
#define DEFINE_REGISTER(name, dwarf_id, size, offset, type, format) name
#include "detail/registers.inc"
#undef DEFINE_REGISTER
    };

    /// @brief 寄存器类型
    enum class register_type
    {
        gpr,
        sub_gpr,
        fpr,
        dr,
    };

    /// @brief 寄存器值的类型
    enum class register_format
    {
        uint,
        double_float,
        long_double,
        vector,
    };

    /// @brief 寄存器信息
    struct register_info
    {
        register_id id;
        std::string_view name;
        // 每个寄存器在特定 ABI（此处为 System V ABI）下都有一个由 DWARF 标准定义的编号
        std::int32_t dwarf_id;
        // 寄存器的宽度，以字节为单位（例如，rax 是 8 字节，eax 是 4 字节，xmm0 是 16 字节）
        std::size_t size;
        // 在 user 结构体中的字节偏移量
        std::size_t offset;
        // 寄存器类别
        register_type type;
        // 寄存器值的类型
        register_format format;
    };

    /// @brief 寄存器信息数组
    constexpr register_info g_register_infos[] = {
#define DEFINE_REGISTER(name, dwarf_id, size, offset, type, format)                                                                                            \
    register_info                                                                                                                                              \
    {                                                                                                                                                          \
        register_id::name, #name, dwarf_id, size, offset, type, format                                                                                         \
    }
#include "detail/registers.inc"
#undef DEFINE_REGISTER
    };

    /// @brief 通过函数对象查找寄存器信息
    /// @tparam F 函数对象类型
    /// @param f 函数对象
    /// @return 寄存器信息
    template <typename F>
        requires std::is_invocable_v<F, const register_info&>
    const register_info& register_info_by(F f)
    {
        if (auto it = std::ranges::find_if(g_register_infos, f); it != std::end(g_register_infos))
        {
            return *it;
        }
        error::send("Can not find register info");
    }

    /// @brief 通过寄存器 ID 查找寄存器信息
    /// @param id 寄存器 ID
    /// @return 寄存器信息
    inline const register_info& register_info_by_id(register_id id)
    {
        return register_info_by([id](const auto& info) { return info.id == id; });
    }

    /// @brief 通过寄存器名称查找寄存器信息
    /// @param name 寄存器名称
    /// @return 寄存器信息
    inline const register_info& register_info_by_name(std::string_view name)
    {
        return register_info_by([name](const auto& info) { return info.name == name; });
    }

    /// @brief 通过 DWARF ID 查找寄存器信息
    /// @param dwarf_id DWARF ID
    /// @return 寄存器信息
    inline const register_info& register_info_by_dwarf_id(std::int32_t dwarf_id)
    {
        return register_info_by([dwarf_id](const auto& info) { return info.dwarf_id == dwarf_id; });
    }

} // namespace ldb
