#pragma once

#include "libldb/types.hpp"
#include <elf.h>

#include <filesystem>
#include <vector>
#include <string_view>
#include <optional>
#include <unordered_map>
namespace ldb
{
	class Elf
	{
	public:
		Elf(const std::filesystem::path& path);
		~Elf();

		Elf(const Elf&) = delete;
		Elf& operator=(const Elf&) = delete;
		Elf(Elf&&) = delete;
		Elf& operator=(Elf&&) = delete;

		std::filesystem::path Path() const { return path; }

		VirtAddr LoadBias() const { return loadBias; }
		void NotifyLoaded(VirtAddr addr) { loadBias = addr; }

		const Elf64_Ehdr& GetHeader() const { return header; }

		std::string_view GetSectionName(std::size_t index) const;

		std::optional<const Elf64_Shdr*> GetSection(std::string_view name) const;

		Span<const std::byte> GetSectionContents(std::string_view name) const;

		std::string_view GetString(std::size_t index) const;

		const Elf64_Shdr* GetSectionContainingAddress(FileAddr addr) const;
		const Elf64_Shdr* GetSectionContainingAddress(VirtAddr addr) const;

		std::optional<FileAddr> GetSectonStartAddress(std::string_view name) const;

	private:
		void ParseSectionHeaders();

		void BuildSectionMap();

		void ParseSymbolTable();

		int fd;
		std::filesystem::path path;
		std::size_t fileSize;
		std::byte* data;
		Elf64_Ehdr header;
		VirtAddr loadBias;

		std::vector<Elf64_Shdr> sectionHeaders;

		std::unordered_map<std::string_view, Elf64_Shdr*> sectionMap;

		std::vector<Elf64_Sym> symbolTable;
	};
}