#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <cxxabi.h>

#include <algorithm>

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
		BuildSymbolMaps();
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

	std::vector<const Elf64_Sym*> Elf::GetSymbolByName(std::string_view name) const
	{
		auto [begin, end] = symbolNameMap.equal_range(name);
		std::vector<const Elf64_Sym*> ret;
		std::transform(begin, end, std::back_inserter(ret), [](const auto& p) { return p.second;});
		return ret;
	}

	std::optional<const Elf64_Sym*> Elf::GetSymbolAtAddress(FileAddr addr) const
	{
		if (addr.ElfFile() != this) return std::nullopt;
		if (auto it = symbolAddrMap.find({addr, {}}); it != symbolAddrMap.end())
		{
			return it->second;
		}
		return std::nullopt;
	}
	std::optional<const Elf64_Sym*> Elf::GetSymbolAtAddress(VirtAddr addr) const
	{
		return GetSymbolAtAddress(addr.ToFileAddr(*this));
	}

	std::optional<const Elf64_Sym*> Elf::GetSymbolContainingAddress(FileAddr addr) const
	{
		if (addr.ElfFile() != this || symbolAddrMap.empty()) return std::nullopt;

		auto it = symbolAddrMap.lower_bound({addr, {}});
		if (it != symbolAddrMap.end())
		{
			if (auto [k, v] = *it; k.first == addr)
			{
				return v;
			}
		}
		if (it == symbolAddrMap.begin()) return std::nullopt;
		--it;
		if (auto [k, v] = *it; k.first < addr && addr < k.second)
		{
			return v;
		}
		return std::nullopt;
	}

	std::optional<const Elf64_Sym*> Elf::GetSymbolContainingAddress(VirtAddr addr) const
	{
		return GetSymbolContainingAddress(addr.ToFileAddr(*this));
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
		auto optTab = GetSection(".symtab");
		if (!optTab)
		{
			optTab = GetSection(".dynsym");
			if (!optTab) return;
		}
		auto symtab = *optTab;
		symbolTable.resize(symtab->sh_size / symtab->sh_entsize);
		std::copy(data + symtab->sh_offset,
			data + symtab->sh_offset + symtab->sh_size,
			reinterpret_cast<std::byte*>(symbolTable.data())
		);
	}

	void Elf::BuildSymbolMaps()
	{
		for (auto& sym : symbolTable)
		{
			auto mangledName = GetString(sym.st_name);
			int status;
			auto demangledName = abi::__cxa_demangle(mangledName.data(), nullptr, nullptr, &status);
			if (status == 0)
			{
				symbolNameMap.emplace(demangledName, &sym);
				free(demangledName);
			}
			symbolNameMap.emplace(mangledName, &sym);

			if (sym.st_value != 0 && sym.st_name != 0 && ELF64_ST_TYPE(sym.st_info) != STT_TLS)
			{
				auto addrRange = std::make_pair(FileAddr{ *this, sym.st_value }, FileAddr{ *this, sym.st_value + sym.st_size });
				symbolAddrMap.emplace(addrRange, &sym);
			}
		}
			
	}
}