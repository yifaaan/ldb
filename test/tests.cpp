#include <elf.h>
#include <fcntl.h>
#include <fmt/format.h>

#include <catch2/catch_test_macros.hpp>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <libldb/bit.hpp>
#include <libldb/error.hpp>
#include <libldb/pipe.hpp>
#include <libldb/process.hpp>
#include <libldb/syscalls.hpp>
#include <libldb/target.hpp>
#include <regex>

using namespace ldb;
namespace
{
    bool process_exists(pid_t pid)
    {
        auto ret = kill(pid, 0);
        return ret != -1 && errno != ESRCH;
    }

    char get_process_status(pid_t pid)
    {
        std::ifstream stat{"/proc/" + std::to_string(pid) + "/stat"};
        std::string data;
        std::getline(stat, data);
        auto index_of_last_parenthesis = data.rfind(')');
        auto index_of_status = index_of_last_parenthesis + 2;
        return data[index_of_status];
    }

    /// @brief 获取段加载偏差
    /// @param path 文件路径
    /// @param file_addr 文件中的地址
    /// @return 段加载偏差
    std::int64_t get_section_load_bias(std::filesystem::path path, Elf64_Addr file_addr)
    {
        auto command = fmt::format("readelf -WS {}", path.c_str());
        auto pipe = popen(command.c_str(), "r");
        char* line = nullptr;
        size_t len = 0;
        std::regex text_regex{R"(PROGBITS\s+(\w+)\s+(\w+)\s+(\w+))"};
        while (getline(&line, &len, pipe) != -1)
        {

            std::cmatch group;
            if (std::regex_search(line, group, text_regex))
            {
                auto address = std::stol(group[1], nullptr, 16);
                auto offset = std::stol(group[2], nullptr, 16);
                auto size = std::stol(group[3], nullptr, 16);
                // fmt::print("address: {}, offset: {}, size: {}\n", address, offset, size);
                if (address <= file_addr && file_addr < address + size)
                {

                    free(line);
                    pclose(pipe);
                    return address - offset;
                }
            }
            free(line);
            line = nullptr;
        }
        pclose(pipe);
        ldb::error::send("Could not find section load bias for address");
    }

    /// @brief 获取入口点在文件中的偏移量
    /// @param path 文件路径
    /// @return 入口点在文件中的偏移量
    std::int64_t get_entry_point_file_offset(std::filesystem::path path)
    {
        std::ifstream elf_file{path};
        Elf64_Ehdr header;
        elf_file.read(reinterpret_cast<char*>(&header), sizeof(header));
        return header.e_entry - get_section_load_bias(path, header.e_entry);
    }

    ldb::virt_addr get_load_address(pid_t pid, std::int64_t file_offset)
    {
        std::ifstream maps_file{fmt::format("/proc/{}/maps", pid)};
        std::regex text_regex{R"((\w+)-\w+ ..(.). (\w+))"};
        std::string line;
        while (std::getline(maps_file, line))
        {
            std::smatch group;
            if (std::regex_search(line, group, text_regex))
            {
                if (group[2] == "x")
                {
                    auto start = std::stol(group[1], nullptr, 16);
                    auto offset = std::stol(group[3], nullptr, 16);
                    return ldb::virt_addr{static_cast<std::uint64_t>(file_offset - offset + start)};
                }
            }
        }
        ldb::error::send("Could not find load address for file offset");
    }
} // namespace

TEST_CASE("validate environment")
{
    REQUIRE(true);
}

TEST_CASE("process::launch success", "[process]")
{
    auto proc = process::launch("yes");
    REQUIRE(process_exists(proc->pid()));
}

TEST_CASE("process::launch no such program", "[process]")
{
    REQUIRE_THROWS_AS(process::launch("no such program"), error);
}

TEST_CASE("process::attach success", "[process]")
{
    auto target = process::launch("targets/run_endlessly", false);
    auto proc = process::attach(target->pid());
    REQUIRE(get_process_status(target->pid()) == 't');
}

TEST_CASE("process::attach invalid PID", "[process]")
{
    REQUIRE_THROWS_AS(process::attach(0), error);
}

TEST_CASE("process::resume success", "[process]")
{
    {
        auto proc = process::launch("targets/run_endlessly");
        proc->resume();
        auto status = get_process_status(proc->pid());
        auto success = status == 'R' || status == 'S';
        REQUIRE(success);
    }

    {
        auto target = process::launch("targets/run_endlessly", false);
        auto proc = process::attach(target->pid());
        proc->resume();
        auto status = get_process_status(proc->pid());
        auto success = status == 'R' || status == 'S';
        REQUIRE(success);
    }
}

TEST_CASE("process::resume already terminated", "[process]")
{
    auto proc = process::launch("targets/end_immediately");
    proc->resume();
    proc->wait_on_signal();
    REQUIRE_THROWS_AS(proc->resume(), error);
}

TEST_CASE("Write register works", "[register]")
{
    bool close_on_exec = false;
    auto channel = ldb::pipe{close_on_exec};

    auto proc = process::launch("targets/reg_write", true, channel.get_write());
    channel.close_write();
    proc->resume(); // 在调用完getpid后停止
    proc->wait_on_signal();

    // 写入rsi寄存器，接着reg_write进程会printf它的内容
    auto& regs = proc->get_registers();
    regs.write_by_id(register_id::rsi, 0xaaaaffff);
    proc->resume();
    proc->wait_on_signal();
    // 读取reg_write进程的输出
    auto output = channel.read();
    REQUIRE(to_string_view(output) == "0xaaaaffff");

    // 写入mm0寄存器，接着reg_write进程会printf它的内容
    regs.write_by_id(register_id::mm0, 0xaaaaffff);
    proc->resume();
    proc->wait_on_signal();
    output = channel.read();
    REQUIRE(to_string_view(output) == "0xaaaaffff");

    // 写入xmm0寄存器，接着reg_write进程会printf它的内容
    regs.write_by_id(register_id::xmm0, 42.22);
    proc->resume();
    proc->wait_on_signal();
    output = channel.read();
    REQUIRE(to_string_view(output) == "42.22");

    // 写入st0寄存器，接着reg_write进程会printf它的内容
    regs.write_by_id(register_id::st0, 42.22l);
    regs.write_by_id(register_id::fsw, std::uint16_t{0b0011100000000000});
    regs.write_by_id(register_id::ftw, std::uint16_t{0b0011111111111111});
    proc->resume();
    proc->wait_on_signal();
    output = channel.read();
    REQUIRE(to_string_view(output) == "42.22");
}

TEST_CASE("Read register works", "[register]")
{
    auto proc = process::launch("targets/reg_read");
    auto& regs = proc->get_registers();

    proc->resume();
    proc->wait_on_signal(); // 在 movq $0xaaaaffff, %r13 之后陷入
    REQUIRE(regs.read_by_id_as<std::uint64_t>(register_id::r13) == 0xaaaaffff);

    proc->resume();
    proc->wait_on_signal(); // 在 movb $8, %r13b 之后陷入
    REQUIRE(regs.read_by_id_as<std::uint8_t>(register_id::r13b) == 8);

    proc->resume();
    proc->wait_on_signal(); // 在 movq %r13, %mm0 之后陷入
    REQUIRE(regs.read_by_id_as<byte64>(register_id::mm0) == to_byte64(0xaaaaffffull));

    proc->resume();
    proc->wait_on_signal(); // 在 movsd my_double(%rip), %xmm0 之后陷入
    REQUIRE(regs.read_by_id_as<byte128>(register_id::xmm0) == to_byte128(64.125));

    proc->resume();
    proc->wait_on_signal(); // 在 fldl my_double(%rip) 之后陷入
    REQUIRE(regs.read_by_id_as<long double>(register_id::st0) == 64.125L);
}

TEST_CASE("Can create breakpoint site", "[breakpoint]")
{
    auto proc = process::launch("targets/run_endlessly");
    auto& site = proc->create_breakpoint_site(virt_addr{42});
    REQUIRE(site.address().addr() == 42);
}

TEST_CASE("Breakpoint site ids increase", "[breakpoint]")
{
    auto proc = process::launch("targets/run_endlessly");
    auto& site1 = proc->create_breakpoint_site(virt_addr{42});
    REQUIRE(site1.address().addr() == 42);
    auto& site2 = proc->create_breakpoint_site(virt_addr{43});
    REQUIRE(site2.id() == site1.id() + 1);
    auto& site3 = proc->create_breakpoint_site(virt_addr{44});
    REQUIRE(site3.id() == site1.id() + 2);
    auto& site4 = proc->create_breakpoint_site(virt_addr{45});
    REQUIRE(site4.id() == site1.id() + 3);
}

TEST_CASE("Can find breakpoint site", "[breakpoint]")
{
    auto proc = process::launch("targets/run_endlessly");
    proc->create_breakpoint_site(virt_addr{42});
    proc->create_breakpoint_site(virt_addr{43});
    proc->create_breakpoint_site(virt_addr{44});
    proc->create_breakpoint_site(virt_addr{45});

    auto& s1 = proc->breakpoint_sites().get_by_address(virt_addr{42});
    auto& s2 = proc->breakpoint_sites().get_by_address(virt_addr{43});
    auto& s3 = proc->breakpoint_sites().get_by_id(s1.id() + 2);
    auto& s4 = proc->breakpoint_sites().get_by_id(s1.id() + 3);
    REQUIRE(proc->breakpoint_sites().contains_address(virt_addr{42}));
    REQUIRE(proc->breakpoint_sites().contains_address(virt_addr{43}));
    REQUIRE(proc->breakpoint_sites().contains_address(virt_addr{44}));
    REQUIRE(proc->breakpoint_sites().contains_address(virt_addr{45}));
    REQUIRE(s1.address().addr() == 42);
    REQUIRE(s2.address().addr() == 43);
    REQUIRE(s3.address().addr() == 44);
    REQUIRE(s4.address().addr() == 45);
}

TEST_CASE("Can not find breakpoint site", "[breakpoint]")
{
    auto proc = process::launch("targets/run_endlessly");

    REQUIRE_THROWS_AS(proc->breakpoint_sites().get_by_address(virt_addr{42}), error);
    REQUIRE_THROWS_AS(proc->breakpoint_sites().get_by_id(42), error);
}

TEST_CASE("Breakpoint list size and empty", "[breakpoint]")
{
    auto proc = process::launch("targets/run_endlessly");
    REQUIRE(proc->breakpoint_sites().size() == 0);
    REQUIRE(proc->breakpoint_sites().empty());

    proc->create_breakpoint_site(virt_addr{42});
    REQUIRE(proc->breakpoint_sites().size() == 1);
    REQUIRE_FALSE(proc->breakpoint_sites().empty());

    proc->create_breakpoint_site(virt_addr{43});
    proc->create_breakpoint_site(virt_addr{44});
    REQUIRE(proc->breakpoint_sites().size() == 3);
    REQUIRE_FALSE(proc->breakpoint_sites().empty());
}

TEST_CASE("Can iterator breakpoint sites", "[breakpoint]")
{
    auto p = process::launch("targets/run_endlessly");
    auto& proc = *p;
    const auto& cproc = *p;
    p->create_breakpoint_site(virt_addr{42});
    p->create_breakpoint_site(virt_addr{43});
    p->create_breakpoint_site(virt_addr{44});
    p->create_breakpoint_site(virt_addr{45});

    proc.breakpoint_sites().for_each(
    [addr = 42](auto& site) mutable
    {
        REQUIRE(site.address().addr() == addr++);
    });

    cproc.breakpoint_sites().for_each(
    [addr = 42](auto& site) mutable
    {
        REQUIRE(site.address().addr() == addr++);
    });
}

TEST_CASE("Breakpoint on address works", "[breakpoint]")
{
    bool close_on_exec = false;
    auto channel = ldb::pipe{close_on_exec};

    auto proc = process::launch("targets/hello_ldb", true, channel.get_write());
    channel.close_write();

    auto file_offset = get_entry_point_file_offset("targets/hello_ldb");
    auto load_address = get_load_address(proc->pid(), file_offset);

    proc->create_breakpoint_site(load_address).enable();
    proc->resume(); // 在int3指令处停止

    auto reason = proc->wait_on_signal(); // wait_on_signal会pc = pc - 1
    REQUIRE(reason.reason == process_state::stopped);
    REQUIRE(reason.info == SIGTRAP);
    REQUIRE(proc->get_pc() == load_address);

    proc->resume(); // 关闭int3指令，单步执行完再恢复
    reason = proc->wait_on_signal();
    REQUIRE(reason.reason == process_state::exited);
    REQUIRE(reason.info == 0);
}

TEST_CASE("Reading and writing memory works", "[memory]")
{
    bool close_on_exec = false;
    auto channel = ldb::pipe{close_on_exec};

    auto proc = process::launch("targets/memory", true, channel.get_write());
    channel.close_write();
    proc->resume();
    proc->wait_on_signal();
    auto a_address = from_bytes<std::uint64_t>(channel.read().data());
    auto a_value_bytes = proc->read_memory(virt_addr{a_address}, 8);
    auto a = from_bytes<std::uint64_t>(a_value_bytes.data());
    REQUIRE(a == 0xcafecafe);

    proc->resume();
    proc->wait_on_signal();
    auto b_address = from_bytes<std::uint64_t>(channel.read().data());
    proc->write_memory(virt_addr{b_address}, {reinterpret_cast<const std::byte*>("Hello, World!"), 14});
    proc->resume();
    proc->wait_on_signal();
    auto b_value_bytes = channel.read();
    REQUIRE(to_string_view(b_value_bytes) == "Hello, World!");
}

TEST_CASE("Hardware breakpoint evades memory checksums", "[breakpoint]")
{
    bool close_on_exec = false;
    auto channel = ldb::pipe{close_on_exec};

    auto proc = process::launch("targets/anti_debugger", true, channel.get_write());
    channel.close_write();

    proc->resume(); // 将函数地址写入管道，并触发SIGTRAP
    proc->wait_on_signal();
    auto func_addr_bytes = channel.read();
    auto func_addr = from_bytes<std::uint64_t>(func_addr_bytes.data());

    // 设置int3
    auto& soft = proc->create_breakpoint_site(virt_addr{func_addr});
    soft.enable();
    proc->resume(); // checksum 不相等，向管道写入"Someone is trying to debug me!"，并触发SIGTRAP
    proc->wait_on_signal();
    REQUIRE(to_string_view(channel.read()) == "Someone is trying to debug me!\n");

    // 设置硬件断点
    proc->breakpoint_sites().remove_by_id(soft.id());
    auto& hard = proc->create_breakpoint_site(virt_addr{func_addr}, true);
    hard.enable();
    proc->resume(); // checksum 相等，向管道写入"Putting pineapple on pizza..."，并触发SIGTRAP
    proc->wait_on_signal();
    REQUIRE(proc->get_pc().addr() == func_addr);
    proc->resume();
    proc->wait_on_signal();
    REQUIRE(to_string_view(channel.read()) == "Putting pineapple on pizza...\n");
}

TEST_CASE("watchpoint detects read", "[watchpoint]")
{
    bool close_on_exec = false;
    auto channel = ldb::pipe{close_on_exec};

    auto proc = process::launch("targets/anti_debugger", true, channel.get_write());
    channel.close_write();

    proc->resume(); // 将函数地址写入管道，并触发SIGTRAP
    proc->wait_on_signal();
    auto func_addr_bytes = channel.read();
    auto func_addr = from_bytes<std::uint64_t>(func_addr_bytes.data());

    auto& watch = proc->create_watchpoint(virt_addr{func_addr}, stoppoint_mode::read_write, 1);
    watch.enable();

    proc->resume(); // 在accumulate读取第一个字节时，触发watchpoint
    proc->wait_on_signal();
    proc->step_instruction(); // accumulate完整计算checksum

    // 修改func_addr处第一个字节为0xcc
    auto& soft = proc->create_breakpoint_site(virt_addr{func_addr});
    soft.enable();

    proc->resume(); // checksum==safe，执行an_innocent_function，触发软断点
    auto reason = proc->wait_on_signal();
    REQUIRE(reason.reason == process_state::stopped);
    REQUIRE(proc->get_pc().addr() == func_addr);

    proc->resume(); // std::puts("Putting pineapple on pizza...")执行完后; 触发42:raisse(SIGRAP)
    reason = proc->wait_on_signal();
    REQUIRE(to_string_view(channel.read()) == "Putting pineapple on pizza...\n");
}

TEST_CASE("syscall mapping works", "[syscall]")
{
    REQUIRE(syscall_id_to_name(0) == "read");
    REQUIRE(syscall_id_to_name(1) == "write");
    REQUIRE(syscall_id_to_name(2) == "open");
    REQUIRE(syscall_id_to_name(3) == "close");
    REQUIRE(syscall_id_to_name(4) == "stat");
    REQUIRE(syscall_id_to_name(5) == "fstat");
    REQUIRE(syscall_id_to_name(62) == "kill");
}

TEST_CASE("Syscall catchpoint works", "[catchpoint]")
{
    auto dev_null_fd = ::open("/dev/null", O_WRONLY);
    auto proc = process::launch("targets/anti_debugger", true, dev_null_fd);
    auto write_syscall_id = syscall_name_to_id("write");
    auto policy = syscall_catch_policy::catch_some({write_syscall_id});
    proc->set_syscall_catch_policy(policy);

    proc->resume(); // 在write(STDOUT_FILENO, &func_ptr, sizeof(void*));TRAP
    auto reason = proc->wait_on_signal();
    REQUIRE(reason.reason == process_state::stopped);
    REQUIRE(reason.info == SIGTRAP); // 注意：启用 TRACESYSGOOD 后，这里应该是 SIGTRAP | 0x80，
                                     // 但 augment_stop_reason 将其改回了 SIGTRAP。
    REQUIRE(reason.trap_reason.has_value());
    REQUIRE(reason.trap_reason.value() == trap_type::syscall);
    REQUIRE(reason.syscall_info.has_value());
    REQUIRE(reason.syscall_info->id == write_syscall_id);
    REQUIRE(reason.syscall_info->entry);

    proc->resume(); // 从系统调用进入点恢复执行，期望在同一个 write 系统调用退出时停止
    reason = proc->wait_on_signal();
    REQUIRE(reason.reason == process_state::stopped);
    REQUIRE(reason.info == SIGTRAP);
    REQUIRE(reason.trap_reason.has_value());
    REQUIRE(reason.trap_reason.value() == trap_type::syscall);
    REQUIRE(reason.syscall_info.has_value());
    REQUIRE(reason.syscall_info->id == write_syscall_id);
    REQUIRE_FALSE(reason.syscall_info->entry);
    close(dev_null_fd);
}

TEST_CASE("Elf parse works", "[elf]")
{
    ldb::elf elf{"targets/hello_ldb"};
    auto entry = elf.header().e_entry;
    auto symbol = elf.get_symbol_at_address(file_addr{elf, entry});
    REQUIRE(symbol.has_value());
    auto name = elf.get_string(symbol.value()->st_name);
    REQUIRE(name == "_start");
    auto symbols = elf.get_symbols_by_name("_start");
    name = elf.get_string(symbols.at(0)->st_name);
    REQUIRE(name == "_start");

    elf.notify_loaded(virt_addr{0xcafecafe});
    symbol = elf.get_symbol_at_address(virt_addr{0xcafecafe + entry});
    name = elf.get_string(symbol.value()->st_name);
    REQUIRE(name == "_start");
}
