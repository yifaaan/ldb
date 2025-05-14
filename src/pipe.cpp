#include <libldb/pipe.hpp>
#include <libldb/error.hpp>
#include <utility>

#include <unistd.h>
#include <fcntl.h>

ldb::pipe::pipe(bool close_on_exec)
{
    if (pipe2(fds_, close_on_exec ? O_CLOEXEC : 0) < 0)
    {
        error::send_errno("Pipe creation failed");
    }
}

ldb::pipe::~pipe()
{
    close_read();
    close_write();
}

int ldb::pipe::release_read()
{
    return std::exchange(fds_[read_fd], -1);
}

int ldb::pipe::release_write()
{
    return std::exchange(fds_[write_fd], -1);
}

void ldb::pipe::close_read()
{
    if (fds_[read_fd] != -1)
    {
        close(fds_[read_fd]);
        fds_[read_fd] = -1;
    }
}

void ldb::pipe::close_write()
{
    if (fds_[write_fd] != -1)
    {
        close(fds_[write_fd]);
        fds_[write_fd] = -1;
    }
}

std::vector<std::byte> ldb::pipe::read()
{
    char buf[1024];
    int chars_read = 0;
    if ((chars_read = ::read(fds_[read_fd], buf, sizeof(buf))) < 0)
    {
        error::send_errno("Could not read from pipe");
    }
    auto bytes = reinterpret_cast<std::byte*>(buf);
    return std::vector<std::byte>{bytes, bytes + chars_read};
}

void ldb::pipe::write(std::byte* from, std::size_t n_bytes)
{
    if (::write(fds_[write_fd], from, n_bytes) < 0)
    {
        error::send_errno("Could not write to pipe");
    }
}