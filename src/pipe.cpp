#include <cstddef>
#include <unistd.h>
#include <fcntl.h>
#include <vector>
#include <utility>

#include <libldb/pipe.hpp>
#include <libldb/error.hpp>


ldb::Pipe::Pipe(bool closeOnExec)
{
    if (pipe2(fds, closeOnExec ? O_CLOEXEC : 0) < 0)
    {
        Error::SendErrno("Pipe creation failed");
    }
}

ldb::Pipe::~Pipe()
{
    CloseRead();
    CloseWrite();
}

int ldb::Pipe::ReleaseRead()
{
    return std::exchange(fds[readFd], -1);
}

int ldb::Pipe::ReleaseWrite()
{
    return std::exchange(fds[writeFd], -1);
}

void ldb::Pipe::CloseRead()
{
    if (fds[readFd] != -1)
    {
        close(fds[readFd]);
        fds[readFd] = -1;
    }
}

void ldb::Pipe::CloseWrite()
{
    if (fds[writeFd] != -1)
    {
        close(fds[writeFd]);
        fds[writeFd] = -1;
    }
}

std::vector<std::byte> ldb::Pipe::Read()
{
    char buf[1024];
    int charsRead;

    if ((charsRead = ::read(fds[readFd], buf, sizeof(buf))) < 0)
    {
        Error::SendErrno("Could not read from pipe");
    }

    auto bytes = reinterpret_cast<std::byte*>(buf);
    return std::vector<std::byte>(bytes, bytes + charsRead);
}

void ldb::Pipe::Write(std::byte* from, std::size_t bytes)
{
    if (::write(fds[writeFd], from, bytes) < 0)
    {
        Error::SendErrno("Could not write to pipe");
    }
}