#pragma once

#include <sys/types.h>
#include <sys/user.h>

#include <cstdint>
#include <filesystem>
#include <libldb/bit.hpp>
#include <libldb/breakpoint_site.hpp>
#include <libldb/registers.hpp>
#include <libldb/stoppoint_collection.hpp>
#include <memory>
#include <optional>

namespace ldb
{
    /// @brief 进程状态
    enum class process_state
    {
        stopped,
        running,
        exited,
        terminated,
    };

    /// @brief 进程停止原因
    struct stop_reason
    {
        explicit stop_reason(int wait_status);

        // 进程停止后的状态
        process_state reason;
        uint8_t info;
    };

    /// @brief 进程
    class process
    {
    public:
        /// @brief 启动进程
        /// @param path 进程路径
        /// @param debug 是否调试
        /// @return 进程指针
        static std::unique_ptr<process> launch(std::filesystem::path path, bool debug = true, std::optional<int> stdout_replacement = std::nullopt);

        /// @brief 附加进程
        /// @param pid 进程 ID
        /// @return 进程指针
        static std::unique_ptr<process> attach(pid_t pid);

        process() = delete;
        process(const process&) = delete;
        process& operator=(const process&) = delete;
        process(process&&) = delete;
        process& operator=(process&&) = delete;
        ~process();

        /// @brief 恢复进程
        void resume();

        /// @brief 等待信号
        /// @return 进程停止原因
        stop_reason wait_on_signal();

        /// @brief 进程状态
        process_state state() const
        {
            return state_;
        }

        /// @brief 进程 ID
        pid_t pid() const
        {
            return pid_;
        }

        /// @brief 获取寄存器
        registers& get_registers()
        {
            return *registers_;
        }

        /// @brief 获取寄存器
        const registers& get_registers() const
        {
            return *registers_;
        }

        /// @brief 获取程序计数器
        virt_addr get_pc() const
        {
            return virt_addr{get_registers().read_by_id_as<std::uint64_t>(register_id::rip)};
        }

        /// @brief 设置程序计数器
        /// @param pc 地址
        void set_pc(virt_addr pc)
        {
            get_registers().write_by_id<std::uint64_t>(register_id::rip, pc.addr());
        }

        /// @brief 写入用户区域, 用于写入寄存器值
        /// @param offset 偏移量
        /// @param data 数据
        void write_user_area(std::size_t offset, std::uint64_t data);

        /// @brief 写入浮点寄存器
        /// @param fprs 浮点寄存器
        void write_fprs(const user_fpregs_struct& fprs);

        /// @brief 写入通用寄存器
        /// @param gprs 通用寄存器
        void write_gprs(const user_regs_struct& gprs);

        /// @brief 创建内存位置断点
        /// @param address 地址
        /// @param is_hardware 是否是硬件断点
        /// @param is_internal 是否是内部断点
        /// @return 断点
        breakpoint_site& create_breakpoint_site(virt_addr address, bool is_hardware = false, bool is_internal = false);

        /// @brief 获取内存位置断点集合
        stoppoint_collection<breakpoint_site>& breakpoint_sites()
        {
            return breakpoint_sites_;
        }

        /// @brief 获取内存位置断点集合
        const stoppoint_collection<breakpoint_site>& breakpoint_sites() const
        {
            return breakpoint_sites_;
        }

        /// @brief 单步执行指令
        /// @return 进程停止原因
        stop_reason step_instruction();

        /// @brief 读取内存
        /// @param address 地址
        /// @param size 大小
        /// @return 内存
        std::vector<std::byte> read_memory(virt_addr address, std::size_t size) const;

        /// @brief 读取内存, 不读取暂停点, 用于反汇编
        /// @param address 地址
        /// @param size 大小
        /// @return 修复后的指令数据
        std::vector<std::byte> read_memory_without_traps(virt_addr address, std::size_t size) const;

        /// @brief 写入内存
        /// @param address 地址
        /// @param data 数据
        void write_memory(virt_addr address, span<const std::byte> data);

        template <typename T>
        T read_memory_as(virt_addr address)
        {
            return from_bytes<T>(read_memory(address, sizeof(T)).data());
        }

        /// @brief 设置硬件执行断点
        /// @param id 断点 ID
        /// @param address 地址
        /// @return 硬件断点索引: dr0-dr3
        int set_hardware_breakpoint(breakpoint_site::id_type id, virt_addr address);

        /// @brief 清除硬件断点
        /// @param index 硬件断点索引: dr0-dr3
        void clear_hardware_stoppoint(int index);

    private:
        process(pid_t pid, bool terminate_on_end, bool is_attached, std::optional<int> stdout_replacement = std::nullopt)
            : pid_{pid}
            , terminate_on_end_{terminate_on_end}
            , is_attached_{is_attached}
            , registers_{new registers(*this)}
        {
        }

        /// @brief 读取所有寄存器
        void read_all_registers();

        /// @brief 设置硬件断点
        /// @param address 地址
        /// @param mode 触发模式
        /// @param size 大小
        /// @return 硬件断点索引: dr0-dr3
        int set_hardware_stoppoint(virt_addr address, stoppoint_mode mode, std::size_t size);

        /// @brief 进程 ID
        pid_t pid_ = 0;

        /// @brief 是否在进程结束时终止
        bool terminate_on_end_ = true;

        /// @brief 进程状态
        process_state state_ = process_state::stopped;

        /// @brief 是否附加
        bool is_attached_ = false;

        /// @brief 寄存器
        std::unique_ptr<registers> registers_;

        /// @brief 内存位置断点集合
        stoppoint_collection<breakpoint_site> breakpoint_sites_;
    };
} // namespace ldb
