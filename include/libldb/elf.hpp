#pragma once

#include <elf.h>

#include <filesystem>
#include <vector>

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

		const Elf64_Ehdr& GetHeader() const { return header; }
	private:
		void ParseSectionHeaders();

		int fd;
		std::filesystem::path path;
		std::size_t fileSize;
		std::byte* data;
		Elf64_Ehdr header;

		std::vector<Elf64_Shdr> sectionHeaders;
	};
}