#include <iostream>

#include <libldb/bit.hpp>
#include <libldb/registers.hpp>
#include <libldb/process.hpp>

namespace
{
	using namespace ldb;

	/// <summary>
	/// widen the bit of the value to fit the reigster sizes
	/// </summary>
	/// <typeparam name="T"></typeparam>
	/// <param name="info"></param>
	/// <param name="t">the value writed into register</param>
	/// <returns></returns>
	template <typename T>
	Byte128 Widen(const RegisterInfo& info, T t)
	{
		if constexpr (std::is_floating_point_v<T>)
		{
			if (info.format == RegisterFormat::doubleFloat) return ToByte128(static_cast<double>(t));
			if (info.format == RegisterFormat::longDouble) return ToByte128(static_cast<long double>(t));
		}
		else if constexpr (std::is_signed_v<T>)
		{
			if (info.format == RegisterFormat::uint)
			{
				switch (info.size)
				{
				case 2: return ToByte128(static_cast<std::int16_t>(t));
				case 4: return ToByte128(static_cast<std::int32_t>(t));
				case 8: return ToByte128(static_cast<std::int64_t>(t));
				}
			}
		}
		return ToByte128(t);
	}
}

namespace ldb
{
	Registers::Value Registers::Read(const RegisterInfo& info) const
	{
		auto bytes = AsBytes(data);

		if (info.format == RegisterFormat::uint)
		{
			switch (info.size)
			{
			case 1: return FromBytes<std::uint8_t>(bytes + info.offset);
			case 2: return FromBytes<std::uint16_t>(bytes + info.offset);
			case 4: return FromBytes<std::uint32_t>(bytes + info.offset);
			case 8: return FromBytes<std::uint64_t>(bytes + info.offset);
			default: Error::Send("Unexpected register size");
			}
		}
		else if (info.format == RegisterFormat::doubleFloat)
		{
			return FromBytes<double>(bytes + info.offset);
		}
		else if (info.format == RegisterFormat::longDouble)
		{
			return FromBytes<long double>(bytes + info.offset);
		}
		else if (info.format == RegisterFormat::vector && info.size == 8)
		{
			return FromBytes<Byte64>(bytes + info.offset);
		}
		else
		{
			return FromBytes<Byte128>(bytes + info.offset);
		}
	}

	void Registers::Write(const RegisterInfo& info, Value val)
	{
		// update the data
		auto bytes = AsBytes(data);
		std::visit([&](auto& v)
		{
			if (sizeof(v) <= info.size)
			{
				// widen to fit the target register
				auto wide = Widen(info, v);
				auto valBytes = AsBytes(wide);
				std::copy(valBytes, valBytes + info.size, bytes + info.offset);
			}
			else
			{
				std::cerr << "ldb::Registers::Write called with mismatched register and value size";
				std::terminate();
			}
		}, val);

		// write to the inferior's registers
		// PTRACE_PEEKUSER and PTRACE_POKEUSER require the addresses to align to 8 bytes
		// they don¡¯t support writing and reading from the x87 area on x64
		// so write all fprs at once
		if (info.type == RegisterType::fpr)
		{
			process->WriteFprs(data.i387);
		}
		else
		{
			// write the single GPR or debug register value
			auto alignOffset = info.offset & ~0b111;
			process->WriteUserArea(alignOffset, FromBytes<std::uint64_t>(bytes + alignOffset));
		}
	}
}