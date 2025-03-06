#include <elf.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <libldb/elf.hpp>
#include <libldb/error.hpp>

#include "libldb/bit.hpp"

ldb::Elf::Elf(const std::filesystem::path& path) : path_{path} {
  if ((fd_ = open(path.c_str(), O_LARGEFILE | O_RDONLY)) < 0) {
    Error::SendErrno("Could not open ELF file");
  }

  struct stat stats;
  if (fstat(fd_, &stats) < 0) {
    Error::SendErrno("Could not retrieve ELF file stats");
  }
  file_size_ = stats.st_size;

  void* ret;
  if ((ret = mmap(nullptr, file_size_, PROT_READ, MAP_SHARED, fd_, 0))) {
    close(fd_);
    Error::SendErrno("Could not mmap ELF file");
  }
  data_ = reinterpret_cast<std::byte*>(ret);

  // Copy the ELF header into the header_ member
  std::copy(data_, data_ + sizeof(header_), AsBytes(header_));

  ParseSectionHeaders();
}

ldb::Elf::~Elf() {
  munmap(data_, file_size_);
  close(fd_);
}

void ldb::Elf::ParseSectionHeaders() {
  // if a file has 0xff00 sections or more, it sets
  // e_shnum to 0 and stores the number of sections in the sh_size field of the
  // first section header.
  auto n_header = header_.e_shnum;
  if (n_header == 0 && header_.e_shentsize != 0) {
    n_header = FromBytes<Elf64_Shdr>(data_ + header_.e_shoff).sh_size;
  }
  section_headers_.resize(n_header);
  std::copy(data_ + header_.e_shoff,
            data_ + header_.e_shoff + n_header * sizeof(Elf64_Shdr),
            reinterpret_cast<std::byte*>(section_headers_.data()));
}