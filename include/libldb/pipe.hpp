#pragma once

#include <vector>

namespace ldb {
class Pipe {
 public:
  explicit Pipe(bool close_on_exec);
  ~Pipe();

  int GetRead() const { return fds[readFd]; }
  int GetWrite() const { return fds[writeFd]; }

  int ReleaseRead();
  int ReleaseWrite();

  void CloseRead();
  void CloseWrite();

  std::vector<std::byte> Read();
  void Write(std::byte* from, std::size_t bytes);

 private:
  static constexpr unsigned readFd = 0;
  static constexpr unsigned writeFd = 1;
  int fds[2];
};
}  // namespace ldb