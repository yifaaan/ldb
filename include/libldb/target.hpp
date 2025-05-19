#pragma once

#include <filesystem>
#include <libldb/elf.hpp>
#include <libldb/process.hpp>
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

    private:
        target(std::unique_ptr<process> process, std::unique_ptr<elf> elf)
            : process_{std::move(process)}
            , elf_{std::move(elf)}
        {
        }

        std::unique_ptr<process> process_;
        std::unique_ptr<elf> elf_;
    };
} // namespace ldb
