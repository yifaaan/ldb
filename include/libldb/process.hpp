#ifndef LDB_PROCESS_HPP
#define LDB_PROCESS_HPP

#include <filesystem>
#include <libldb/breakpoint_site.hpp>
#include <libldb/registers.hpp>
#include <libldb/stoppoint_collection.hpp>
#include <memory>
#include <optional>
#include <sys/types.h>
#include <sys/user.h>
#include <vector>

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
        stop_reason(int wait_status);

        process_state reason;
        std::uint8_t info;
    };

    class process
    {
    public:
        static std::unique_ptr<process> launch(std::filesystem::path path, bool debug = true,
                                               std::optional<int> stdout_replacement = std::nullopt);
        static std::unique_ptr<process> attach(pid_t pid);

        process() = delete;
        process(const process&) = delete;
        process& operator=(const process&) = delete;
        ~process();
        void resume();
        stop_reason wait_on_signal();

        pid_t pid() const
        {
            return pid_;
        }
        process_state state() const
        {
            return state_;
        }

        registers& get_registers()
        {
            return *registers_;
        }
        const registers& get_registers() const
        {
            return *registers_;
        }
        // Get PC register
        virt_addr get_pc() const
        {
            return virt_addr{get_registers().read_by_id_as<std::uint64_t>(register_id::rip)};
        }
        /// With the register value offset and the data.
        /// We just wrote into our own user struct.
        void write_user_area(std::size_t offset, std::uint64_t data);

        void write_fprs(const user_fpregs_struct& fprs);
        void write_gprs(const user_regs_struct& gprs);

        breakpoint_site& create_breakpoint_site(virt_addr address);
        stoppoint_collection<breakpoint_site>& breakpoint_sites()
        {
            return breakpoint_sites_;
        }
        const stoppoint_collection<breakpoint_site>& breakpoint_sites() const
        {
            return breakpoint_sites_;
        }

        void set_pc(virt_addr address)
        {
            get_registers().write_by_id(register_id::rip, address.addr());
        }

        ldb::stop_reason step_instruction();

    private:
        /// For static member fn to construct a process
        process(pid_t pid, bool terminate_on_end, bool is_attached)
            : pid_(pid), terminate_on_end_(terminate_on_end), is_attached_(is_attached),
              registers_(new registers(*this))
        {
        }

        void read_all_registers();

    private:
        pid_t pid_ = 0;
        bool terminate_on_end_ = true;
        bool is_attached_ = true;
        process_state state_ = process_state::stopped;
        std::unique_ptr<registers> registers_;
        /// We can’t store a std::vector<breakpoint_site> because we can’t copy or move a breakpoint site,
        /// but we get around this by
        /// dynamically allocating them and storing std::unique_ptr values instead
        stoppoint_collection<breakpoint_site> breakpoint_sites_;
    };

} // namespace ldb

#endif
