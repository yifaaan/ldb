#ifndef LDB_ERROR_HPP
#define LDB_ERROR_HPP

#include <cstring>
#include <stdexcept>
#include <string>

namespace ldb {
class error : public std::runtime_error {
public:
  [[noreturn]] static void send(const std::string& what) { throw error(what); }

  [[noreturn]] static void send_errno(const std::string& prefix) {
    throw error(prefix + std::string(": ") + std::strerror(errno));
  }

private:
  error(const std::string& what) : std::runtime_error(what) {}
};
} // namespace ldb

#endif