
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <libldb/error.hpp>
#include <libldb/pipe.hpp>
#include <libldb/process.hpp>

namespace
{
    // 如果发生错误，则将错误信息写入管道并退出
    void exit_with_perror(ldb::pipe& channel, const std::string& prefix)
    {
        auto message = prefix + ": " + std::strerror(errno);
        channel.write(reinterpret_cast<std::byte*>(message.data()), message.size());
        exit(-1);
    }
}

ldb::stop_reason::stop_reason(int wait_status)
{
    if (WIFEXITED(wait_status))
    {
        // 正常退出
        reason = process_state::exited;
        // 存储进程的退出码
        info = WEXITSTATUS(wait_status);
    }
    else if (WIFSIGNALED(wait_status))
    {
        // 被信号终止
        reason = process_state::terminated;
        // 存储终止进程的信号编
        info = WTERMSIG(wait_status);
    }
    else if (WIFSTOPPED(wait_status))
    {
        // 被信号暂停
        reason = process_state::stopped;
        // 存储导致进程暂停的信号编号
        info = WSTOPSIG(wait_status);
    }
    else
    {
        error::send("Unknown stop reason");
    }
}

std::unique_ptr<ldb::process> ldb::process::launch(std::filesystem::path path)
{
    pipe channel{/*close_on_exec*/ true};

    pid_t pid;
    if ((pid = fork()) < 0)
    {
        error::send_errno("fork failed");
    }
    if (pid == 0)
    {
        channel.close_read();
        if (ptrace(PTRACE_TRACEME, 0, nullptr, nullptr) < 0)
        {
            exit_with_perror(channel, "Tracing failed");
        }
        if (execlp(path.c_str(), path.c_str(), nullptr) < 0)
        {
            exit_with_perror(channel, "execlp failed");
        }
    }
    channel.close_write();
    auto data = channel.read();
    if (!data.empty())
    {
        // 如果管道中存在数据，则说明子进程退出了
        waitpid(pid, nullptr, 0);
        // 将管道中的数据转换为字符串并发送
        auto chars = reinterpret_cast<char*>(data.data());
        error::send(std::string{chars, chars + data.size()});
    }
    std::unique_ptr<ldb::process> proc{new ldb::process{pid, /*terminate_on_end=*/true}};
    proc->wait_on_signal();
    return proc;
}

std::unique_ptr<ldb::process> ldb::process::attach(pid_t pid)
{
    if (pid == 0)
    {
        error::send("Invalid pid");
    }
    if (ptrace(PTRACE_ATTACH, pid, nullptr, nullptr) < 0)
    {
        error::send_errno("Could not attach");
    }
    std::unique_ptr<process> proc{new process{pid, /*terminate_on_end*/ false}};
    proc->wait_on_signal();
    return proc;
}

ldb::process::~process()
{
    if (pid_ != 0)
    {
        int status;
        if (state_ == process_state::running)
        {
            kill(pid_, SIGSTOP);
            waitpid(pid_, &status, 0);
        }
        // Before detached, the inferior process must be stopped.
        ptrace(PTRACE_DETACH, pid_, nullptr, nullptr);
        kill(pid_, SIGCONT);

        if (terminate_on_end_)
        {
            kill(pid_, SIGKILL);
            waitpid(pid_, &status, 0);
        }
    }
}

void ldb::process::resume()
{
    if (ptrace(PTRACE_CONT, pid_, nullptr, nullptr) < 0)
    {
        error::send_errno("Could not resume");
    }
    state_ = process_state::running;
}

ldb::stop_reason ldb::process::wait_on_signal()
{
    int wait_status;
    if (waitpid(pid_, &wait_status, 0) < 0)
    {
        error::send_errno("waitpid failed");
    }
    auto reason = stop_reason{wait_status};
    state_ = reason.reason;
    return reason;
}
