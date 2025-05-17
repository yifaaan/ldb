
#include <sys/personality.h>
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
} // namespace

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

std::unique_ptr<ldb::process> ldb::process::launch(std::filesystem::path path, bool debug, std::optional<int> stdout_replacement)
{
    pipe channel{/*close_on_exec*/ true};

    pid_t pid;
    if ((pid = fork()) < 0)
    {
        error::send_errno("fork failed");
    }
    if (pid == 0)
    {
        // 禁用地址空间布局随机化ASLR,加载基地址在多次运行时会保持一致
        personality(ADDR_NO_RANDOMIZE);
        channel.close_read();
        if (stdout_replacement)
        {
            if (dup2(*stdout_replacement, STDOUT_FILENO) < 0)
            {
                exit_with_perror(channel, "stdout replacement failed");
            }
        }
        if (debug && ptrace(PTRACE_TRACEME, 0, nullptr, nullptr) < 0)
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
    std::unique_ptr<ldb::process> proc{new ldb::process{pid, /*terminate_on_end=*/true, /*is_attached=*/debug}};
    if (debug)
    {
        proc->wait_on_signal();
    }
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
    std::unique_ptr<process> proc{new process{pid, /*terminate_on_end*/ false, /*is_attached*/ true}};
    proc->wait_on_signal();
    return proc;
}

ldb::process::~process()
{
    if (pid_ != 0)
    {
        int status;
        if (is_attached_)
        {
            if (state_ == process_state::running)
            {
                kill(pid_, SIGSTOP);
                waitpid(pid_, &status, 0);
            }
            // Before detached, the inferior process must be stopped.
            ptrace(PTRACE_DETACH, pid_, nullptr, nullptr);
            kill(pid_, SIGCONT);
        }

        if (terminate_on_end_)
        {
            kill(pid_, SIGKILL);
            waitpid(pid_, &status, 0);
        }
    }
}

void ldb::process::resume()
{
    auto pc = get_pc();
    if (breakpoint_sites_.enabled_stoppoint_at_address(pc))
    {
        auto& bp = breakpoint_sites_.get_by_address(pc);
        // 恢复原本的指令内容
        bp.disable();
        // 单步执行该指令
        if (ptrace(PTRACE_SINGLESTEP, pid_, nullptr, nullptr) < 0)
        {
            error::send_errno("Could not single step");
        }
        int wait_status;
        if (waitpid(pid_, &wait_status, 0) < 0)
        {
            error::send_errno("waitpid failed");
        }
        // 重新启用断点
        bp.enable();
    }
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

    // 如果进程是附加的并且状态是停止的, 则读取所有寄存器，更新寄存器值
    if (is_attached_ && state_ == process_state::stopped)
    {
        read_all_registers();
        // in3指令的地址
        auto instr_begin = get_pc() - 1;
        // 上条指令是int3指令，需要恢复pc，在resume时执行正确的指令
        if (reason.info == SIGTRAP && breakpoint_sites_.enabled_stoppoint_at_address(instr_begin))
        {
            set_pc(instr_begin);
        }
    }
    return reason;
}

void ldb::process::read_all_registers()
{
    // 读取通用寄存器
    if (ptrace(PTRACE_GETREGS, pid_, nullptr, &get_registers().data_.regs) < 0)
    {
        error::send_errno("Could not read GPR registers");
    }
    // 读取浮点寄存器
    if (ptrace(PTRACE_GETFPREGS, pid_, nullptr, &get_registers().data_.i387) < 0)
    {
        error::send_errno("Could not read FPR registers");
    }
    // 读取调试寄存器
    for (int i = 0; i < 8; i++)
    {
        auto id = static_cast<int>(register_id::dr0) + i;
        auto& info = register_info_by_id(static_cast<register_id>(id));

        errno = 0;
        std::int64_t data = ptrace(PTRACE_PEEKUSER, pid_, info.offset, nullptr);
        if (errno != 0)
        {
            error::send_errno("Could not read debug registers");
        }
        get_registers().data_.u_debugreg[i] = data;
    }
}

void ldb::process::write_user_area(std::size_t offset, std::uint64_t data)
{
    if (ptrace(PTRACE_POKEUSER, pid_, offset, data) < 0)
    {
        error::send_errno("Could not write to user area");
    }
}

void ldb::process::write_fprs(const user_fpregs_struct& fprs)
{
    if (ptrace(PTRACE_SETFPREGS, pid_, nullptr, &fprs) < 0)
    {
        error::send_errno("Could not write floating point registers");
    }
}

void ldb::process::write_gprs(const user_regs_struct& gprs)
{
    if (ptrace(PTRACE_SETREGS, pid_, nullptr, &gprs) < 0)
    {
        error::send_errno("Could not write general purpose registers");
    }
}

ldb::breakpoint_site& ldb::process::create_breakpoint_site(virt_addr address)
{
    if (breakpoint_sites_.contains_address(address))
    {
        error::send("Breakpoint site already created at address: " + std::to_string(address.addr()));
    }
    return breakpoint_sites_.push(std::unique_ptr<breakpoint_site>{new breakpoint_site{*this, address}});
}

ldb::stop_reason ldb::process::step_instruction()
{
    std::optional<breakpoint_site*> to_reenable;
    auto pc = get_pc();
    if (breakpoint_sites_.enabled_stoppoint_at_address(pc))
    {
        auto& bp = breakpoint_sites_.get_by_address(pc);
        // 禁用断点，恢复原本的指令内容
        bp.disable();
        // 执行完该指令后，需要重新启用断点
        to_reenable = &bp;
    }
    if (ptrace(PTRACE_SINGLESTEP, pid_, nullptr, nullptr) < 0)
    {
        error::send_errno("Could not single step");
    }
    auto reason = wait_on_signal();
    if (to_reenable)
    {
        to_reenable.value()->enable();
    }
    return reason;
}
