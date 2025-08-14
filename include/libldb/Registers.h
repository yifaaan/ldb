#pragma once

#include <variant>

#include <libldb/RegisterInfo.h>
#include <libldb/Types.h>

#include <sys/user.h>


namespace ldb
{
    class Process;

    class Registers
    {
    public:
        Registers() = delete;
        Registers(const Registers&) = delete;
        Registers& operator=(const Registers&) = delete;

        using Value = std::variant<uint8_t, uint16_t, uint32_t, uint64_t, int8_t, int16_t, int32_t, int64_t, float, double, long double, Byte64, Byte128>;

        // Read the value of the register
        Value Read(const RegisterInfo& info) const;
        // Write the value to the register
        void Write(const RegisterInfo& info, Value value);

        template <typename T>
        T ReadByIdAs(RegisterId id) const
        {
            return std::get<T>(Read(RegisterInfoById(id)));
        }

        template <typename T>
        void WriteById(RegisterId id, T value)
        {
            Write(RegisterInfoById(id), value);
        }

    private:
        friend Process;
        Registers(Process& process) : process(&process)
        {
        }

        user data;
        Process* process;
    };
} // namespace ldb