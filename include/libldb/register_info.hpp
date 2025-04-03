#pragma once

#include <sys/user.h>

#include <string_view>
#include <cstddef>
#include <algorithm>
#include <string_view>

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
		{RegisterId::name, #name, dwarf_id, size, offset, type, format},
		#include <libldb/detail/registers.inc>
		#undef DEFINE_REGISTER
	};

	const RegisterInfo& RegisterInfoBy(auto&& f)
	{
		auto it = std::find_if(std::begin(registerInfos), std::end(registerInfos), f);
		if (it == std::end(registerInfos))
		{
			Error::Send("Can't find register info");
		}
		return *it;
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