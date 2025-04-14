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
	}

	Elf::~Elf()
	{
		munmap(data, fileSize);
		close(fd);
	}
}