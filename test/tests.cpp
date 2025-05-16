#include <catch2/catch_test_macros.hpp>
#include <csignal>
#include <fstream>
#include <libldb/bit.hpp>
#include <libldb/error.hpp>
#include <libldb/pipe.hpp>
#include <libldb/process.hpp>

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
    regs.write_by_id_as(register_id::rsi, 0xaaaaffff);
    proc->resume();
    proc->wait_on_signal();
    // 读取reg_write进程的输出
    auto output = channel.read();
    REQUIRE(to_string_view(output) == "0xaaaaffff");

    // 写入mm0寄存器，接着reg_write进程会printf它的内容
    regs.write_by_id_as(register_id::mm0, 0xaaaaffff);
    proc->resume();
    proc->wait_on_signal();
    output = channel.read();
    REQUIRE(to_string_view(output) == "0xaaaaffff");

    // 写入xmm0寄存器，接着reg_write进程会printf它的内容
    regs.write_by_id_as(register_id::xmm0, 42.22);
    proc->resume();
    proc->wait_on_signal();
    output = channel.read();
    REQUIRE(to_string_view(output) == "42.22");

    // 写入st0寄存器，接着reg_write进程会printf它的内容
    regs.write_by_id_as(register_id::st0, 42.22l);
    regs.write_by_id_as(register_id::fsw, std::uint16_t{0b0011100000000000});
    regs.write_by_id_as(register_id::ftw, std::uint16_t{0b0011111111111111});
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
