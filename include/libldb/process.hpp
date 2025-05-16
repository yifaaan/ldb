#pragma once

#include <sys/types.h>
#include <sys/user.h>

#include <cstdint>
#include <filesystem>
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
        /// @return 断点
        breakpoint_site& create_breakpoint_site(virt_addr address);

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
