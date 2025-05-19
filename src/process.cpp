
#include <sys/personality.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <libldb/bit.hpp>
#include <libldb/error.hpp>
#include <libldb/pipe.hpp>
#include <libldb/process.hpp>

namespace
{
    /// 如果发生错误，则将错误信息写入管道并退出
    void exit_with_perror(ldb::pipe& channel, const std::string& prefix)
    {
        auto message = prefix + ": " + std::strerror(errno);
        channel.write(reinterpret_cast<std::byte*>(message.data()), message.size());
        exit(-1);
    }

    /// 编码硬件断点模式
    std::uint64_t encode_hardware_stoppoint_mode(ldb::stoppoint_mode mode)
    {
        switch (mode)
        {
        case ldb::stoppoint_mode::execute:
            return 0b00;
        case ldb::stoppoint_mode::write:
            return 0b01;
        case ldb::stoppoint_mode::read_write:
            return 0b11;
        default:
            ldb::error::send("Invalid stoppoint mode");
        }
    }

    // 编码硬件断点大小
    std::uint64_t encode_hardware_stoppoint_size(std::size_t size)
    {
        switch (size)
        {
        case 1:
            return 0b00;
        case 2:
            return 0b01;
        case 8:
            return 0b10;
        case 4:
            return 0b11;
        default:
            ldb::error::send("Invalid stoppoint size");
        }
    }

    /// 找到空闲的调试寄存器编号0-3
    int find_free_stoppoint_register(std::uint64_t dr7)
    {
        for (int i = 0; i < 4; i++)
        {
            if ((dr7 & (std::uint64_t{0b11} << (i * 2))) == 0)
                return i;
        }
        ldb::error::send("No free stoppoint register");
    }

    /// 设置ptrace选项: 跟踪系统调用
    /// 如果一个 SIGTRAP 是因为系统调用进入或退出而产生的，那么内核传递给 waitpid 的状态信息，
    /// 在通过 WSTOPSIG(status) 提取信号时，将不再仅仅是 SIGTRAP，而是
    /// SIGTRAP | 0x80。
    void set_ptrace_options(pid_t pid)
    {
        if (ptrace(PTRACE_SETOPTIONS, pid, nullptr, PTRACE_O_TRACESYSGOOD) < 0)
        {
            ldb::error::send_errno("Could not set ptrace options: TRACESYSGOOD");
        }
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
        if (setpgid(0, 0) < 0)
        {
            exit_with_perror(channel, "setpgid failed");
        }
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
        set_ptrace_options(pid);
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
    set_ptrace_options(pid);
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
    auto request = (syscall_catch_policy_.mode_ == syscall_catch_policy::mode::none) ? PTRACE_CONT : PTRACE_SYSCALL;
    // PTRACE_SYSCALL:下一次进入系统调用或从系统调用退出时再次停止，并向调试器发送一个 SIGTRAP (如果 PTRACE_O_TRACESYSGOOD 已设置，则信号为 SIGTRAP | 0x80)
    if (ptrace(request, pid_, nullptr, nullptr) < 0)
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
        // 补充导致SIGTRAP停止的原因
        augment_stop_reason(reason);
        // in3指令的地址
        auto instr_begin = get_pc() - 1;
        // 上条指令是int3指令，需要恢复pc，在resume时执行正确的指令
        if (reason.info == SIGTRAP && breakpoint_sites_.enabled_stoppoint_at_address(instr_begin))
        {
            if (reason.trap_reason)
            {
                switch (*reason.trap_reason)
                {
                case trap_type::software_break:
                    {
                        if (breakpoint_sites_.enabled_stoppoint_at_address(instr_begin))
                        {
                            set_pc(instr_begin);
                        }
                        break;
                    }
                case trap_type::hardware_break:
                    {
                        auto id_variant = get_current_hardware_stoppoint();
                        if (id_variant.index() == 1)
                        {
                            watchpoints_.get_by_id(std::get<1>(id_variant)).update_data();
                        }
                        break;
                    }
                case trap_type::single_step:
                    break;
                case trap_type::syscall:
                    reason = maybe_resume_from_syscall(reason);
                    break;
                case trap_type::unknown:
                    break;
                }
            }
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

ldb::breakpoint_site& ldb::process::create_breakpoint_site(virt_addr address, bool is_hardware, bool is_internal)
{
    if (breakpoint_sites_.contains_address(address))
    {
        error::send("Breakpoint site already created at address: " + std::to_string(address.addr()));
    }
    return breakpoint_sites_.push(std::unique_ptr<breakpoint_site>{new breakpoint_site{*this, address, is_hardware, is_internal}});
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

std::vector<std::byte> ldb::process::read_memory(virt_addr address, std::size_t size) const
{
    std::vector<std::byte> ret(size);
    iovec local_desc{ret.data(), size};
    std::vector<iovec> remote_desc;
    while (size > 0)
    {
        auto up_to_next_page = 0x1000 - (address.addr() & 0xfff);
        auto chunk_size = std::min(up_to_next_page, size);
        remote_desc.push_back({reinterpret_cast<void*>(address.addr()), chunk_size});
        address += chunk_size;
        size -= chunk_size;
    }
    if (process_vm_readv(pid_, &local_desc, 1, remote_desc.data(), remote_desc.size(), 0) < 0)
    {
        error::send_errno("Could not read memory");
    }
    return ret;
}

std::vector<std::byte> ldb::process::read_memory_without_traps(virt_addr address, std::size_t size) const
{
    auto memory = read_memory(address, size);
    auto sites = breakpoint_sites_.get_in_region(address, address + size);
    for (auto site : sites)
    {
        // 硬件断点并未替换int3指令
        if (!site->is_enabled() || !site->is_hardware())
        {
            continue;
        }
        auto offset = site->address().addr() - address.addr();
        memory[offset] = site->saved_data_;
    }
    return memory;
}

void ldb::process::write_memory(virt_addr address, span<const std::byte> data)
{
    std::size_t written = 0;
    while (written < data.size())
    {
        auto remaining = data.size() - written;
        std::uint64_t word;
        if (remaining >= 8)
        {
            word = from_bytes<std::uint64_t>(data.begin() + written);
        }
        else
        {
            // ptrace一次写入8个字节
            auto read = read_memory(address + written, 8);
            // 写入的8个字节中，前remaining个字节是data中的，剩余的8 - remaining个字节是read中的
            std::memcpy(reinterpret_cast<char*>(&word), data.begin() + written, remaining);
            std::memcpy(reinterpret_cast<char*>(&word) + remaining, read.data() + remaining, 8 - remaining);
        }
        if (ptrace(PTRACE_POKEDATA, pid_, address.addr() + written, word) < 0)
        {
            error::send_errno("Could not write memory");
        }
        written += 8;
    }
}

int ldb::process::set_hardware_stoppoint(virt_addr address, stoppoint_mode mode, std::size_t size)
{
    auto& regs = get_registers();
    auto dr7 = regs.read_by_id_as<std::uint64_t>(register_id::dr7);
    // 找到空闲的调试寄存器编号0-3
    int free_space = find_free_stoppoint_register(dr7);
    auto id = static_cast<int>(register_id::dr0) + free_space;
    // 设置触发地址, 写入drx寄存器
    regs.write_by_id(static_cast<register_id>(id), address.addr());

    // 00b: stoppoint_mode::execute
    // 01b: stoppoint_mode::write
    // 11b: stoppoint_mode::read_write
    auto mode_flag = encode_hardware_stoppoint_mode(mode);

    // 00b: size:1 字节
    // 01b: size:2 字节
    // 10b: size:8 字节
    // 11b: size:4 字节，对于执行断点，大小必须设为 1 字节（00b）
    auto size_flag = encode_hardware_stoppoint_size(size);

    // 计算启用位、模式位和大小位
    auto enable_bits = (std::uint64_t{1} << (free_space * 2));
    auto mode_bits = (mode_flag << (free_space * 4 + 16));
    auto size_bits = (size_flag << (free_space * 2 + 18));

    // 清除旧的位
    auto clear_mask = (std::uint64_t{0b11} << (free_space * 2)) | (std::uint64_t{0b1111} << (free_space * 4 + 16));
    dr7 &= ~clear_mask;
    dr7 |= enable_bits | mode_bits | size_bits;
    regs.write_by_id(register_id::dr7, dr7);
    return free_space;
}

int ldb::process::set_hardware_breakpoint(breakpoint_site::id_type id, virt_addr address)
{
    return set_hardware_stoppoint(address, stoppoint_mode::execute, 1);
}

void ldb::process::clear_hardware_stoppoint(int index)
{
    // 清除drx中的地址
    auto dr0_3 = static_cast<register_id>(static_cast<int>(register_id::dr0) + index);
    get_registers().write_by_id(dr0_3, std::uint64_t{0});
    // 清除dr7中的相关位
    auto dr7 = get_registers().read_by_id_as<std::uint64_t>(register_id::dr7);
    auto clear_mask = (std::uint64_t{0b11} << (index * 2)) | (std::uint64_t{0b1111} << (index * 4 + 16));
    dr7 &= ~clear_mask;
    get_registers().write_by_id(register_id::dr7, dr7);
}

int ldb::process::set_watchpoint(watchpoint::id_type id, virt_addr address, stoppoint_mode mode, std::size_t size)
{
    return set_hardware_stoppoint(address, mode, size);
}

ldb::watchpoint& ldb::process::create_watchpoint(virt_addr address, stoppoint_mode mode, std::size_t size)
{
    if (watchpoints_.contains_address(address))
    {
        error::send("Watchpoint already created at address: " + std::to_string(address.addr()));
    }
    return watchpoints_.push(std::unique_ptr<watchpoint>{new watchpoint{*this, address, mode, size}});
}

void ldb::process::augment_stop_reason(stop_reason& reason)
{
    siginfo_t sig_info_data;
    // 获取导致SIGTRAP的详细信息
    if (ptrace(PTRACE_GETSIGINFO, pid_, nullptr, &sig_info_data) < 0)
    {
        error::send_errno("Could not get signal information");
    }
    // 如果SIGTRAP是由于系统调用引起的，则需要获取系统调用信息
    if (reason.info == (SIGTRAP | 0x80))
    {
        // TODO:获取系统调用信息
        auto& sys_info = reason.syscall_info.emplace();
        auto& regs = get_registers();

        if (expecting_syscall_exit_)
        {
            // 退出系统调用时停止
            sys_info.entry = false;
            sys_info.id = regs.read_by_id_as<std::uint64_t>(register_id::orig_rax);
            sys_info.ret = regs.read_by_id_as<std::uint64_t>(register_id::rax);
            expecting_syscall_exit_ = false;
        }
        else
        {
            // 进入系统调用时停止
            sys_info.entry = true;
            sys_info.id = regs.read_by_id_as<std::uint64_t>(register_id::orig_rax);
            std::array<register_id, 6> arg_regs{register_id::rdi, register_id::rsi, register_id::rdx, register_id::rcx, register_id::r8, register_id::r9};
            for (int i = 0; i < 6; i++)
            {
                sys_info.args[i] = regs.read_by_id_as<std::uint64_t>(arg_regs[i]);
            }
            expecting_syscall_exit_ = true;
        }
        reason.info = SIGTRAP;
        reason.trap_reason = trap_type::syscall;
        return;
    }
    expecting_syscall_exit_ = false;
    if (reason.info == SIGTRAP)
    {
        reason.trap_reason = trap_type::unknown;
        switch (sig_info_data.si_code)
        {
        case TRAP_TRACE:
            reason.trap_reason = trap_type::single_step;
            break;
        case SI_KERNEL:
            reason.trap_reason = trap_type::software_break;
            break;
        case TRAP_HWBKPT:
            reason.trap_reason = trap_type::hardware_break;
            break;
        default:
            break;
        }
    }
}

std::variant<ldb::breakpoint_site::id_type, ldb::watchpoint::id_type> ldb::process::get_current_hardware_stoppoint() const
{
    auto& regs = get_registers();
    auto dr6 = regs.read_by_id_as<std::uint64_t>(register_id::dr6);
    auto hit_drx_index = __builtin_ctzll(dr6);
    auto drx_id = static_cast<register_id>(static_cast<int>(register_id::dr0) + hit_drx_index);
    // 读取断点的地址
    auto stop_address = regs.read_by_id_as<std::uint64_t>(drx_id);

    using return_type = std::variant<breakpoint_site::id_type, watchpoint::id_type>;
    if (breakpoint_sites_.contains_address(virt_addr{stop_address}))
    {
        auto site_id = breakpoint_sites_.get_by_address(virt_addr{stop_address}).id();
        return return_type{std::in_place_index<0>, site_id};
    }
    else if (watchpoints_.contains_address(virt_addr{stop_address}))
    {
        auto watch_id = watchpoints_.get_by_address(virt_addr{stop_address}).id();
        return return_type{std::in_place_index<1>, watch_id};
    }
    auto watch_id = watchpoints_.get_by_address(virt_addr{stop_address}).id();
    return return_type{std::in_place_index<1>, watch_id};
}

ldb::stop_reason ldb::process::maybe_resume_from_syscall(const stop_reason& reason)
{
    if (syscall_catch_policy_.mode_ == syscall_catch_policy::mode::some)
    {
        // 导致停止的系统调用编号是否在捕获列表中
        auto& to_catch = syscall_catch_policy_.get_to_catch();
        auto found = std::ranges::find(to_catch, reason.syscall_info->id);
        if (found == to_catch.end())
        {
            // 当前停止的系统调用不是用户请求追踪的那个，需要恢复执行
            resume();
            return wait_on_signal();
        }
    }
    return reason;
}
