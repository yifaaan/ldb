#pragma once

#include <sys/types.h>

#include <cstdint>
#include <filesystem>
#include <memory>

namespace ldb
{
    enum class process_state
    {
        stopped,
        running,
        exited,
        terminated,
    };

    struct stop_reason
    {
        explicit stop_reason(int wait_status);

        // 进程停止后的状态
        process_state reason;
        uint8_t info;
    };

    class process
    {
    public:
        static std::unique_ptr<process> launch(std::filesystem::path path, bool debug = true);
        static std::unique_ptr<process> attach(pid_t pid);

        process() = delete;
        process(const process&) = delete;
        process& operator=(const process&) = delete;
        process(process&&) = delete;
        process& operator=(process&&) = delete;
        ~process();

        void resume();

        stop_reason wait_on_signal();

        process_state state() const
        {
            return state_;
        }

        pid_t pid() const
        {
            return pid_;
        }

    private:
        process(pid_t pid, bool terminate_on_end, bool is_attached)
            : pid_{pid}
            , terminate_on_end_{terminate_on_end}
            , is_attached_{is_attached}
        {
        }

        pid_t pid_ = 0;
        bool terminate_on_end_ = true;
        process_state state_ = process_state::stopped;
        bool is_attached_ = false;
    };
} // namespace ldb
