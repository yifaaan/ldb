#ifndef LDB_REGISTERS_HPP
#define LDB_REGISTERS_HPP

#include <sys/user.h>
#include <variant>

#include <libldb/types.hpp>
#include <libldb/register_info.hpp>

namespace ldb
{
    class Process;
    class Registers
    {
    public:
        Registers() = delete;
        Registers(const Registers&) = delete;
        Registers& operator=(const Registers&) = delete;

        using value = std::variant<
                std::uint8_t, std::uint16_t, std::uint32_t, std::uint64_t,
                std::int8_t, std::int16_t, std::int32_t, std::int64_t,
                float, double, long double,
                byte64, byte128>;

        value Read(const RegisterInfo& info) const;
        
        void Write(const RegisterInfo& info, value val);


        
        template<typename T>
        T ReadByIdAs(RegisterId id) const
        {
            return std::get<T>(Read(RegisterInfoById(id)));
        }

        void WriteById(RegisterId id, value val)
        {
            Write(RegisterInfoById(id), val);
        }

    private:
        friend Process;
        Registers(Process& _proc) : proc(&_proc) {}

    private:
        /// parent reads data into this
        user data;
        /// parent
        Process* proc;
    };
}

#endif