#pragma once

#include <sys/user.h>

#include <libldb/register_info.hpp>
#include <libldb/types.hpp>
#include <variant>

namespace ldb
{
    class process;
    /// @brief 寄存器
    class registers
    {
    public:
        registers() = delete;
        registers(const process& proc) = delete;
        registers& operator=(const process& proc) = delete;
        registers(process&& proc) = delete;
        registers& operator=(process&& proc) = delete;
        ~registers() = default;

        /// @brief 寄存器值的类型
        using value = std::variant<std::uint64_t,
                                   std::uint32_t,
                                   std::uint16_t,
                                   std::uint8_t,
                                   std::int64_t,
                                   std::int32_t,
                                   std::int16_t,
                                   std::int8_t,
                                   double,
                                   long double,
                                   byte64,
                                   byte128>;

        /// @brief 读取寄存器值
        /// @param info 寄存器信息
        /// @return 寄存器值
        value read(const register_info& info) const;

        /// @brief 写入寄存器值
        /// @param info 寄存器信息
        /// @param v 寄存器值
        void write(const register_info& info, value v);

        /// @brief 读取寄存器值
        /// @tparam T 寄存器值的类型
        /// @param id 寄存器 ID
        /// @return 寄存器值
        template <typename T>
            requires std::is_convertible_v<T, value>
        T read_by_id_as(register_id id) const
        {
            return std::get<T>(read(register_info_by_id(id)));
        }

        /// @brief 写入寄存器值
        /// @tparam T 寄存器值的类型
        /// @param id 寄存器 ID
        /// @param v 寄存器值
        template <typename T>
            requires std::is_convertible_v<T, value>
        void write_by_id(register_id id, T v)
        {
            write(register_info_by_id(id), v);
        }

    private:
        friend process;

        explicit registers(process& proc)
            : proc_{&proc}
        {
        }

        /// @brief 所属进程
        process* proc_;
        /// @brief 寄存器数据
        user data_;
    };

} // namespace ldb
