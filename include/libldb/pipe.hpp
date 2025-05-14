#pragma once

#include <cstddef>
#include <vector>

namespace ldb
{
    class pipe
    {
    public:
        explicit pipe(bool close_on_exec);
        ~pipe();

        auto get_read() const { return fds_[read_fd]; }
        auto get_write() const { return fds_[write_fd]; }

        int release_read();
        int release_write();

        void close_read();
        void close_write();

        std::vector<std::byte> read();

        void write(std::byte* from, std::size_t n_bytes);

    private:
        static constexpr auto read_fd = 0;
        static constexpr auto write_fd = 1;
        int fds_[2];
    };
}
