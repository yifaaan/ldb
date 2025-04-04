#pragma once


#include <sys/user.h>
#include <variant>

#include <libldb/register_info.hpp>
#include <libldb/types.hpp>


namespace ldb
{
	class Process;
	class Registers
	{
	public:
		Registers() = delete;
		Registers(const Registers&) = delete;
		Registers& operator=(const Registers&) = delete;
		Registers(Registers&&) = delete;
		Registers& operator=(Registers&&) = delete;
		~Registers() = default;

		using Value = std::variant<std::uint8_t, std::uint16_t, std::uint32_t, std::uint64_t,
				std::int8_t, std::int16_t, std::int32_t, std::int64_t,
				float, double, long double,
				Byte64, Byte128>;

		Value Read(const RegisterInfo& info) const;

		void Write(const RegisterInfo& info, Value val);

		template <typename T>
		T ReadByIdAs(RegisterId id) const
		{
			return std::get<T>(Read(RegisterInfoById(id)));
		}

		void WriteById(RegisterId id, Value val)
		{
			Write(RegisterInfoById(id), val);
		}
	private:
		friend Process;
		explicit Registers(Process& proc)
			: process{ &proc }
		{
		}

		user data;
		Process* process;
	};
}