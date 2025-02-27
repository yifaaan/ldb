#include <fcntl.h>
#include <unistd.h>

#include <libldb/error.hpp>
#include <libldb/pipe.hpp>
#include <utility>

ldb::Pipe::Pipe(bool close_on_exec) {
  // close_on_exec
  // Ensure the pipe gets closed when we call execlp.
  if (pipe2(fds_, close_on_exec ? O_CLOEXEC : 0) < 0) {
    Error::SendErrno("Pipe creation failed");
  }
}

ldb::Pipe::~Pipe() {
  CloseRead();
  CloseWrite();
}

int ldb::Pipe::ReleaseRead() { return std::exchange(fds_[read_fd_], -1); }

int ldb::Pipe::ReleaseWrite() { return std::exchange(fds_[write_fd_], -1); }

void ldb::Pipe::CloseRead() {
  if (fds_[read_fd_] != -1) {
    close(fds_[read_fd_]);
    fds_[read_fd_] = -1;
  }
}

void ldb::Pipe::CloseWrite() {
  if (fds_[write_fd_] != -1) {
    close(fds_[write_fd_]);
    fds_[write_fd_] = -1;
  }
}

std::vector<std::byte> ldb::Pipe::Read() {
  char buf[1024];
  int chars_read;
  if ((chars_read = ::read(fds_[read_fd_], buf, sizeof(buf))) < 0) {
    Error::SendErrno("Could not read from pipe");
  }
  auto bytes = reinterpret_cast<std::byte*>(buf);
  return std::vector<std::byte>(bytes, bytes + chars_read);
}

void ldb::Pipe::Write(std::byte* from, std::size_t bytes) {
  if (::write(fds_[write_fd_], from, bytes) < 0) {
    Error::SendErrno("Could not write to the pipe");
  }
}