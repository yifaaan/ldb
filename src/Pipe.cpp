#include <libldb/Pipe.h>

#include <utility>

#include <libldb/Error.h>

#include <unistd.h>
#include <fcntl.h>

namespace ldb
{
    Pipe::Pipe(bool closeOnExec)
    {
        if (pipe2(fds, closeOnExec ? O_CLOEXEC : 0) < 0)
        {
            Error::SendErrno("Pipe creation failed");
        }
    }

    Pipe::~Pipe()
    {
        CloseRead();
        CloseWrite();
    }

    int Pipe::ReleaseRead()
    {
        return std::exchange(fds[readFd], -1);
    }

    int Pipe::ReleaseWrite()
    {
        return std::exchange(fds[writeFd], -1);
    }

    void Pipe::CloseRead()
    {
        if (fds[readFd] != -1)
        {
            close(fds[readFd]);
            fds[readFd] = -1;
        }
    }

    void Pipe::CloseWrite()
    {
        if (fds[writeFd] != -1)
        {
            close(fds[writeFd]);
            fds[writeFd] = -1;
        }
    }

    std::vector<std::byte> Pipe::Read()
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

    void Pipe::Write(std::byte* from, std::size_t bytes)
    {
        if (::write(fds[writeFd], from, bytes) < 0)
        {
            Error::SendErrno("Could not write to pipe");
        }
    }
}