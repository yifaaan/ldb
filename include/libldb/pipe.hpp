#ifndef LDB_PIPE_HPP
#define LDB_PIPE_HPP

#include <vector>
#include <cstddef>

namespace ldb
{
    class Pipe
    {
    private:
        static constexpr unsigned readFd = 0;
        static constexpr unsigned writeFd = 1;
        int fds[2];

    public:
        explicit Pipe(bool closeOnExec);

        ~Pipe();

        int GetRead() const { return fds[readFd]; }
        int GetWrite() const { return fds[writeFd]; }

        int ReleaseRead();
        int ReleaseWrite();
        void CloseRead();
        void CloseWrite();

        std::vector<std::byte> Read();
        void Write(std::byte* from, std::size_t bytes);
    };
}

#endif