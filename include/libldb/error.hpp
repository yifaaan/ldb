#pragma once

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>

namespace ldb {
class Error : public std::runtime_error {
 public:
  [[noreturn]]
  static void Send(const std::string& what) {
    throw Error{what};
  }

  // Throwan error with a prefix and a system error message.
  [[noreturn]]
  static void SendErrno(const std::string& prefix) {
    throw Error{prefix + std::string(": ") + std::strerror(errno)};
  }

 private:
  Error(const std::string& what) : std::runtime_error{what} {}
};
}  // namespace ldb