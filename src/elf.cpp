#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>


#include <libldb/elf.hpp>
#include <libldb/error.hpp>
#include <libldb/bit.hpp>

namespace ldb
{
	Elf::Elf(const std::filesystem::path& _path)
		: path(_path)
	{
		if ((fd = open(path.c_str(), O_LARGEFILE, O_RDONLY)) < 0)
		{
			Error::SendErrno("Could not open ELF file");
		}
		struct stat st;
		if (fstat(fd, &st) < 0)
		{
			Error::SendErrno("Could not retrieve file stats");
		}
		fileSize = st.st_size;
		void* ret;
		if ((ret = mmap(0, fileSize, PROT_READ, MAP_SHARED, fd, 0)) == MAP_FAILED)
		{
			close(fd);
			Error::SendErrno("Could not mmap ELF file");
		}
		data = reinterpret_cast<std::byte*>(ret);
		std::copy(data, data + sizeof(header), AsBytes(header));

		ParseSectionHeaders();
		BuildSectionMap();
		ParseSymbolTable();
	}

	Elf::~Elf()
	{
		munmap(data, fileSize);
		close(fd);
	}



	std::string_view Elf::GetSectionName(std::size_t index) const
	{
		// the section that stores the string table for section names(usually .shstrtab)
		auto& section = sectionHeaders[header.e_shstrndx];
		return { reinterpret_cast<char*>(data)  + section.sh_offset + index };
	}

	std::optional<const Elf64_Shdr*> Elf::GetSection(std::string_view name) const
	{
		if (sectionMap.contains(name))
		{
			return sectionMap.at(name);
		}
		return std::nullopt;
	}

	Span<const std::byte> Elf::GetSectionContents(std::string_view name) const
	{
		if (auto section = GetSection(name); section)
		{
			return { data + section.value()->sh_offset, section.value()->sh_size };
		}
		return { nullptr, std::size_t(0) };
	}

	std::string_view Elf::GetString(std::size_t index) const
	{
		auto strtab = GetSection(".strtab");
		if (!strtab)
		{
			strtab = GetSection(".dynstr");
			if (!strtab) return {};
		}
		return { reinterpret_cast<char*>(data) + strtab.value()->sh_offset + index };
	}

	const Elf64_Shdr* Elf::GetSectionContainingAddress(FileAddr addr) const
	{
		if (addr.ElfFile() != this) return nullptr;
		for (auto& sh : sectionHeaders)
		{
			if (sh.sh_addr <= addr.Addr() && addr.Addr() < sh.sh_addr + sh.sh_size)
			{
				return &sh;
			}
		}
		return nullptr;
	}

	const Elf64_Shdr* Elf::GetSectionContainingAddress(VirtAddr addr) const
	{
		for (auto& sh : sectionHeaders)
		{
			if (LoadBias() + sh.sh_addr <= addr && addr < LoadBias() + sh.sh_addr + sh.sh_size)
			{
				return &sh;
			}
		}
		return nullptr;
	}

	std::optional<FileAddr> Elf::GetSectonStartAddress(std::string_view name) const
	{
		if (auto sh = GetSection(name); sh)
		{
			return FileAddr{ *this, sh.value()->sh_addr };
		}
		return std::nullopt;
	}

	void Elf::ParseSectionHeaders()
	{
		auto nHeaders = header.e_shnum;
		if (nHeaders == 0 && header.e_shentsize != 0)
		{
			nHeaders = FromBytes<Elf64_Shdr>(data + header.e_shoff).sh_size;
		}
		sectionHeaders.resize(nHeaders);
		std::copy(data + header.e_shoff,
			data + header.e_shoff + sizeof(Elf64_Shdr) * nHeaders,
			reinterpret_cast<std::byte*>(sectionHeaders.data())
		);
	}

	void Elf::BuildSectionMap()
	{
		for (auto& section : sectionHeaders)
		{
			sectionMap[GetSectionName(section.sh_name)] = &section;
		}
	}

	void Elf::ParseSymbolTable()
	{

	}

}