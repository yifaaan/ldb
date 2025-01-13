#ifndef LDB_REGISTER_INFO_HPP
#define LDB_REGISTER_INFO_HPP

#include <cstdint>
#include <cstddef>
#include <string_view>
#include <algorithm>
#include <sys/user.h>

#include <libldb/error.hpp>

namespace ldb
{
    enum class RegisterId
    {
        #define DEFINE_REGISTER(name, dwarf_id, size, offset, type, format) name
        #include <libldb/detail/registers.inc>
        #undef DEFINE_REGISTER
    };

    enum class RegisterType
    {
        Gpr,
        /// eax is the 32-bit version of rax
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
        std::int32_t dwarfId;
        std::size_t size;
        std::size_t offset;
        RegisterType type;
        RegisterFormat format;
    };

    /// a global array of the information for every register in the system 
    inline constexpr const RegisterInfo GRegisterInfos[] = 
    {
        #define DEFINE_REGISTER(name, dwarf_id, size, offset, type, format) \
            { RegisterId::name, #name, dwarf_id, size, offset, type, format }
        #include <libldb/detail/registers.inc>
        #undef DEFINE_REGISTER
    };

    template<typename F>
    const RegisterInfo& RegisterInfoBy(F f)
    {
        if (auto it = std::find_if(std::begin(GRegisterInfos), std::end(GRegisterInfos), f); it == std::end(GRegisterInfos))
        {
            ldb::Error::Send("Can't find register info");
        }
        else
        {
            return *it;
        }
    }

    inline const RegisterInfo& RegisterInfoById(RegisterId id)
    {
        return RegisterInfoBy([id](const auto& x) { return x.id == id; });
    }

    inline const RegisterInfo& RegisterInfoByName(std::string_view name)
    {
        return RegisterInfoBy([name](const auto& x) { return x.name == name; });
    }

    inline const RegisterInfo& RegisterInfoByDwarfId(std::int32_t dwarfId)
    {
        return RegisterInfoBy([dwarfId](const auto& x) { return x.dwarfId == dwarfId; });
    }
}

#endif