#pragma once

#include <cstring>
#include <stdexcept>

namespace ldb
{
    class Error : public std::runtime_error
    {
    public:
        [[noreturn]]
        static void Send(const std::string& what)
        {
            throw Error(what);
        }

        [[noreturn]]
        static void SendErrno(const std::string& prefix)
        {
            auto error = std::string(prefix) + std::string(": ") + std::string(std::strerror(errno));
            throw Error(error);
        }

    private:
        Error(const std::string& what) : std::runtime_error(what)
        {
        }
    };
} // namespace ldb