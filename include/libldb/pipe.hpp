#pragma once

#include <vector>
namespace ldb {
class Pipe {
 public:
  explicit Pipe(bool close_on_exec);
  ~Pipe();

  // Get the read file descriptors.
  int GetRead() const { return fds_[read_fd_]; }
  // Get the write file descriptors.
  int GetWrite() const { return fds_[write_fd_]; }
  // Release the read file descriptors.
  int ReleaseRead();
  // Release the write file descriptors.
  int ReleaseWrite();
  // Close the read file descriptors.
  void CloseRead();
  // Close the write file descriptors.
  void CloseWrite();

  // Read from the pipe.
  std::vector<std::byte> Read();
  // Write to the pipe.
  void Write(std::byte* from, std::size_t bytes);

 private:
  static constexpr int unsigned read_fd_ = 0;
  static constexpr int unsigned write_fd_ = 1;
  int fds_[2];
};
}  // namespace ldb