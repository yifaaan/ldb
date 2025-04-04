#pragma once

#include <sys/user.h>

#include <string_view>
#include <cstdint>
#include <algorithm>

#include <libldb/error.hpp>

namespace ldb
{

	enum class RegisterId
	{
		#define DEFINE_REGISTER(name,dwarf_id,size,offset,type,format) name
		#include <libldb/detail/registers.inc>
		#undef DEFINE_REGISTER
	};

	enum class RegisterType
	{
		gpr,
		subGpr,
		fpr,
		dr,
	};

	enum class RegisterFormat
	{
		uint,
		doubleFloat,
		longDouble,
		vector,
	};


	struct RegisterInfo
	{
		RegisterId id;
		std::string_view name;
		std::int32_t dwarfId;
		std::size_t size;
		std::size_t offset;
		RegisterType type;
		RegisterFormat format;
	};

	inline constexpr const RegisterInfo registerInfos[] =
	{
		#define DEFINE_REGISTER(name,dwarf_id,size,offset,type,format) \
		{RegisterId::name, #name, dwarf_id, size, offset, type, format}
		#include <libldb/detail/registers.inc>
		#undef DEFINE_REGISTER
	};

	const RegisterInfo& RegisterInfoBy(auto&& f)
	{
		if (auto it = std::ranges::find_if(registerInfos, f); it != std::end(registerInfos)) return *it;
		Error::Send("Can't find register info");
	}

	inline const RegisterInfo& RegisterInfoById(RegisterId id)
	{
		return RegisterInfoBy([id](const RegisterInfo& info) { return info.id == id; });
	}

	inline const RegisterInfo& RegisterInfoByName(std::string_view name)
	{
		return RegisterInfoBy([name](const RegisterInfo& info) { return info.name == name; });
	}

	inline const RegisterInfo& RegisterInfoByDwarf(std::int32_t dwarfId)
	{
		return RegisterInfoBy([dwarfId](const RegisterInfo& info) { return info.dwarfId == dwarfId; });
	}
}