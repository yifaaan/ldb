#pragma once

#include <stdexcept>
#include <cstring>

namespace ldb
{
	class Error : public std::runtime_error
	{
	public:
		[[noreturn]]
		static void Send(std::string_view what)
		{
			throw Error{ what };
		}

		/// <summary>
		/// use the errno to send an error message
		/// </summary>
		[[noreturn]]
		static void SendErrno(std::string_view prefix)
		{
			throw Error{ std::string{prefix} + ": " + std::strerror(errno)};
		}

	private:
		Error(std::string_view what)
			: std::runtime_error(what.data())
		{
		}
	};
}