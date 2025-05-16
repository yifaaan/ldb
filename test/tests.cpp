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
