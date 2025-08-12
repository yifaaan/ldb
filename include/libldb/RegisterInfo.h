#pragma once

#include <cstdint>
#include <string_view>
// 新增头文件以支持 offsetof、std::find_if、is_invocable_v、user 结构体与错误处理
#include <cstddef>
#include <algorithm>
#include <type_traits>
#include <sys/user.h>
#include <libldb/Error.h>

namespace ldb
{
    enum class RegisterId
    {
#define DEFINE_REGISTER(name, dwarfId, size, offset, type, format) name
#include <libldb/detail/Registers.inc>
#undef DEFINE_REGISTER
    };

    enum class RegisterType
    {
        Gpr,
        SubGpr,
        Fpr,
        Dr,
    };

    enum class RegisterFormat
    {
        UInt,
        DoubleFloat,
        LongDouble,
        Vector,
    };

    struct RegisterInfo
    {
        RegisterId id;
        std::string_view name;
        int32_t dwarfId;
        size_t size;
        size_t offset;
        RegisterType type;
        RegisterFormat format;
    };

    constexpr RegisterInfo registerInfos[] = {
#define DEFINE_REGISTER(name, dwarfId, size, offset, type, format) {RegisterId::name, #name, dwarfId, size, offset, type, format}
#include <libldb/detail/Registers.inc>
#undef DEFINE_REGISTER
    };

    template <typename F>
        requires std::is_invocable_v<F, const RegisterInfo&>
    const RegisterInfo& RegisterInfoBy(F f)
    {
        auto it = std::find_if(std::begin(registerInfos), std::end(registerInfos), f);
        if (it == std::end(registerInfos))
        {
            Error::Send("Can't find register info");
        }
        return *it;
    }

    inline const RegisterInfo& RegisterInfoById(RegisterId id)
    {
        return RegisterInfoBy([id](auto& i) { return i.id == id; });
    }

    inline const RegisterInfo& RegisterInfoByName(std::string_view name)
    {
        return RegisterInfoBy([name](auto& i) { return i.name == name; });
    }

    inline const RegisterInfo& RegisterInfoByDwarf(int32_t dwarfId)
    {
        return RegisterInfoBy([dwarfId](auto& i) { return i.dwarfId == dwarfId; });
    }
} // namespace ldb