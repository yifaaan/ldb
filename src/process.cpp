#include <bits/types/struct_iovec.h>
#include <libldb/bit.hpp>
#include <sys/personality.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <libldb/error.hpp>
#include <libldb/pipe.hpp>
#include <libldb/process.hpp>

namespace
{
    void exit_with_perror(ldb::pipe& channel, const std::string& prefix)
    {
        auto message = prefix + std::string(": ") + std::strerror(errno);
        channel.write(reinterpret_cast<std::byte*>(message.data()), message.size());
        exit(-1);
    }
} // namespace

std::unique_ptr<ldb::process> ldb::process::launch(std::filesystem::path path, bool debug, std::optional<int> stdout_replacement)
{
    // Child auto close channel when exec succeed,
    // because of close_on_exec.
    ldb::pipe channel(/*close_on_exec=*/true);
    pid_t pid;
    if ((pid = fork()) < 0)
    {
        error::send_errno("Fork failed");
    }

    if (pid == 0)
    {
        // Disable ASLR
        personality(ADDR_NO_RANDOMIZE);
        channel.close_read();

        if (stdout_replacement)
        {
            close(STDOUT_FILENO);
            // Anything that goes to stdout now goes through whatever
            // *stdout_replacement points to, which could be a file or a pipe.
            if (dup2(*stdout_replacement, STDOUT_FILENO) < 0)
            {
                exit_with_perror(channel, "stdout replacement failed");
            }
        }
        if (debug and ptrace(PTRACE_TRACEME, 0, nullptr, nullptr) < 0)
        {
            exit_with_perror(channel, "Tracing failed");
        }
        if (execlp(path.c_str(), path.c_str(), nullptr) < 0)
        {
            exit_with_perror(channel, "Exec failed");
        }
    }
    // Parent close the write side of the pipe.
    channel.close_write();
    auto data = channel.read();
    channel.close_read();

    if (data.size() > 0)
    {
        waitpid(pid, nullptr, 0);
        auto chars = reinterpret_cast<char*>(data.data());
        error::send(std::string(chars, chars + data.size()));
    }
    std::unique_ptr<process> proc(new process(pid, /*terminate_on_end=*/true, debug));
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
        error::send("Invalid PID");
    }
    if (ptrace(PTRACE_ATTACH, pid, nullptr, nullptr) < 0)
    {
        error::send_errno("Could not attach");
    }

    std::unique_ptr<process> proc(new process(pid, /*terminate_on_end=*/false, true));
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
    // auto a = ptrace(PTRACE_CONT, pid_, nullptr, nullptr);
    // std::cout << std::format("ptrace ret: {}\n", a);
    auto pc = get_pc();
    if (breakpoint_sites_.enabled_stoppoint_at_address(pc))
    {
        auto& bp = breakpoint_sites_.get_by_address(pc);
        bp.disable();
        if (ptrace(PTRACE_SINGLESTEP, pid_, nullptr, nullptr) < 0)
        {
            error::send_errno("Failed to single step");
        }
        int wait_status;
        if (waitpid(pid_, &wait_status, 0) < 0)
        {
            error::send_errno("waitpid failed");
        }
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
    int options = 0;
    if (waitpid(pid_, &wait_status, options) < 0)
    {
        error::send_errno("waitpid failed");
    }
    stop_reason reason(wait_status);
    state_ = reason.reason;

    if (is_attached_ and state_ == process_state::stopped)
    {
        read_all_registers();

        auto instr_begin = get_pc() - 1;
        if (reason.info == SIGTRAP and breakpoint_sites_.enabled_stoppoint_at_address(instr_begin))
        {
            set_pc(instr_begin);
        }
    }
    return reason;
}

ldb::stop_reason::stop_reason(int wait_status)
{
    if (WIFEXITED(wait_status))
    {
        reason = process_state::exited;
        info = WEXITSTATUS(wait_status);
    }
    else if (WIFSIGNALED(wait_status))
    {
        reason = process_state::terminated;
        info = WTERMSIG(wait_status);
    }
    else if (WIFSTOPPED(wait_status))
    {
        reason = process_state::stopped;
        info = WSTOPSIG(wait_status);
    }
}

void ldb::process::read_all_registers()
{
    // Read the GPRs using PTRACE_GETREGS
    if (ptrace(PTRACE_GETREGS, pid_, nullptr, &get_registers().data_.regs) < 0)
    {
        error::send_errno("Could not read GPR registers");
    }
    if (ptrace(PTRACE_GETFPREGS, pid_, nullptr, &get_registers().data_.i387) < 0)
    {
        error::send_errno("Could not read FPR registers");
    }
    for (int i = 0; i < 8; i++)
    {
        // Read debug registers
        auto id = static_cast<int>(register_id::dr0) + i;
        auto info = register_info_by_id(static_cast<register_id>(id));

        errno = 0;
        std::int64_t data = ptrace(PTRACE_PEEKUSER, pid_, info.offset, nullptr);
        if (errno != 0)
        {
            error::send_errno("Could not read debug register");
        }

        get_registers().data_.u_debugreg[i] = data;
    }
}

/// Call PTRACE_POKEUSER to write the given data to the user area at the given
/// offset.
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
        error::send("Breakpoint site already created at address " + std::to_string(address.addr()));
    }
    return breakpoint_sites_.push(std::unique_ptr<breakpoint_site>(new breakpoint_site(*this, address)));
}

ldb::stop_reason ldb::process::step_instruction()
{
    std::optional<breakpoint_site*> to_reenable;
    auto pc = get_pc();
    if (breakpoint_sites_.enabled_stoppoint_at_address(pc))
    {
        auto& bp = breakpoint_sites_.get_by_address(pc);
        bp.disable();
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

std::vector<std::byte> ldb::process::read_memory(virt_addr address, std::size_t amount) const
{
    std::vector<std::byte> ret(amount);

    iovec local_desc{ret.data(), ret.size()};
    std::vector<iovec> remote_descs;

    while (amount > 0)
    {
        auto up_to_next_page = 0x1000 - (address.addr() & 0xfff);
        auto chunk_size = std::min(amount, up_to_next_page);
        remote_descs.push_back({reinterpret_cast<void*>(address.addr()), chunk_size});
        amount -= chunk_size;
        address += chunk_size;
    }

    if (process_vm_readv(pid_, &local_desc, 1, remote_descs.data(), remote_descs.size(), 0) < 0)
    {
        error::send_errno("Could not read process memory");
    }
    return ret;
}

void ldb::process::write_memory(virt_addr address, span<const std::byte> data)
{
    std::size_t written = 0;
    while (written < data.size())
    {
        auto remaining = data.size() - written;
        // Wrod to write.
        std::uint64_t word;
        if (remaining >= 8)
        {
            word = from_bytes<std::uint64_t>(data.begin() + written);
        }
        else
        {
            // Doing a partial memory write.
            auto read = read_memory(address + written, 8);
            auto word_data = reinterpret_cast<char*>(&word);
            std::memcpy(word_data, data.begin() + written, remaining);
            std::memcpy(word_data + remaining, read.data() + remaining, 8 - remaining);
        }
        if (ptrace(PTRACE_POKEDATA, pid_, address + written, word) < 0)
        {
            error::send_errno("Failed to write memory");
        }
        written += 8;
    }
}