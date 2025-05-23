#pragma once

#include <filesystem>
#include <libldb/elf.hpp>
#include <libldb/process.hpp>
#include <libldb/stack.hpp>
#include <memory>
#include <optional>

namespace ldb
{
    class target
    {
    public:
        target() = delete;
        target(const target&) = delete;
        target& operator=(const target&) = delete;
        target(target&&) = delete;
        target& operator==(target&&) = delete;
        ~target() = default;

        /// @brief 启动一个新进程
        /// @param path 程序路径
        /// @param stdout_replacement 标准输出重定向
        /// @return 目标对象
        static std::unique_ptr<target> launch(std::filesystem::path, std::optional<int> stdout_replacement = std::nullopt);

        /// @brief 附加到一个进程
        /// @param pid 进程 ID
        /// @return 目标对象
        static std::unique_ptr<target> attach(pid_t pid);

        /// @brief 获取进程
        /// @return 进程
        process& get_process()
        {
            return *process_;
        }

        const process& get_process() const
        {
            return *process_;
        }

        elf& get_elf()
        {
            return *elf_;
        }

        const elf& get_elf() const
        {
            return *elf_;
        }

        void notify_stop(const ldb::stop_reason& reason);

        file_addr get_pc_file_address() const;

        stack& get_stack()
        {
            return stack_;
        }

        const stack& get_stack() const
        {
            return stack_;
        }

        ldb::stop_reason step_in();
        ldb::stop_reason step_out();
        ldb::stop_reason step_over();

        /// @brief 获取当前PC处的行号条目
        /// @return 行号条目
        ldb::line_table::iterator line_entry_at_pc() const;

        /// @brief 运行到指定地址
        /// @param address 目标地址
        /// @return 停止原因
        ldb::stop_reason run_until_address(virt_addr address);

    private:
        target(std::unique_ptr<process> process, std::unique_ptr<elf> elf)
            : process_{std::move(process)}
            , elf_{std::move(elf)}
            , stack_{this}
        {
        }

        std::unique_ptr<process> process_;
        std::unique_ptr<elf> elf_;

        stack stack_;
    };
} // namespace ldb
