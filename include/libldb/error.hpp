#ifndef LDB_ERROR_HPP
#define LDB_ERROR_HPP

#include <cerrno>
#include <stdexcept>
#include <cstring>
#include <string>

namespace ldb
{
    class Error : public std::runtime_error
    {
    public:
        [[noreturn]] static void Send(const std::string& what) { throw Error(what); }

        [[noreturn]] static void SendErrno(const std::string& prefix)
        {
            throw Error(prefix + std::string(": ") + std::strerror(errno));
        }

    private:
        Error(const std::string& what) : std::runtime_error(what)
        {}
    };
}

#endif