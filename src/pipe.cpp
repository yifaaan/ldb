#include <fcntl.h>
#include <unistd.h>

#include <libldb/error.hpp>
#include <libldb/pipe.hpp>
#include <utility>

namespace ldb {
Pipe::Pipe(bool close_on_exec) {
  if (pipe2(fds, close_on_exec ? O_CLOEXEC : 0) < 0) {
    Error::SendErrno("Pipe creation failed");
  }
}

Pipe::~Pipe() {
  CloseRead();
  CloseWrite();
}

int Pipe::ReleaseRead() { return std::exchange(fds[readFd], -1); }

int Pipe::ReleaseWrite() { return std::exchange(fds[writeFd], -1); }

void Pipe::CloseRead() {
  if (fds[readFd] != -1) {
    close(fds[readFd]);
    fds[readFd] = -1;
  }
}

void Pipe::CloseWrite() {
  if (fds[writeFd] != -1) {
    close(fds[writeFd]);
    fds[writeFd] = -1;
  }
}

std::vector<std::byte> Pipe::Read() {
  char buf[1024];
  int nread;
  if ((nread = ::read(fds[readFd], buf, sizeof(buf))) < 0) {
    Error::SendErrno("Could not read from pipe");
  }
  auto bytes = reinterpret_cast<std::byte*>(buf);
  return {bytes, bytes + nread};
}

void Pipe::Write(std::byte* from, std::size_t bytes) {
  if (::write(fds[writeFd], from, bytes) < 0) {
    Error::SendErrno("Could not write to pipe");
  }
}
}  // namespace ldb