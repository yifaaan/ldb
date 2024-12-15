#include <fcntl.h>
#include <libldb/error.hpp>
#include <libldb/pipe.hpp>
#include <unistd.h>
#include <utility>

ldb::pipe::pipe(bool close_on_exec) {
  if (pipe2(fds_, close_on_exec ? O_CLOEXEC : 0) < 0) {
    error::send_errno("Pipe creation failed");
  }
}

ldb::pipe::~pipe() {
  close_read();
  close_write();
}

void ldb::pipe::close_read() {
  if (fds_[0] != -1) {
    close(fds_[0]);
    fds_[0] = -1;
  }
}

void ldb::pipe::close_write() {
  if (fds_[1] != -1) {
    close(fds_[1]);
    fds_[1] = -1;
  }
}

int ldb::pipe::release_read() { return std::exchange(fds_[0], -1); }

int ldb::pipe::release_write() { return std::exchange(fds_[1], -1); }

std::vector<std::byte> ldb::pipe::read() {
  char buf[1024];
  int chars_read;
  if ((chars_read = ::read(fds_[read_fd], buf, sizeof(buf))) < 0) {
    error::send_errno("Could not read from pipe");
  }

  auto bytes = reinterpret_cast<std::byte*>(buf);
  return std::vector<std::byte>(bytes, bytes + chars_read);
}

void ldb::pipe::write(std::byte* from, std::size_t bytes) {
  if (::write(fds_[write_fd], from, bytes) < 0) {
    error::send_errno("Could not write to pipe");
  }
}