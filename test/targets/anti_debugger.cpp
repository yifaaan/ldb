#include <unistd.h>

#include <csignal>
#include <cstdio>
#include <numeric>
void AnInnocentFunction() { std::puts("Putting pineapple on pizza..."); }

void AnInnocentFunctionEnd() {}

int Checksum() {
  auto start = reinterpret_cast<volatile const char*>(&AnInnocentFunction);
  auto end = reinterpret_cast<volatile const char*>(&AnInnocentFunctionEnd);
  return std::accumulate(start, end, 0);
}

int main() {
  auto safe = Checksum();

  // Write the address of AnInnocentFunction to stdout
  auto ptr = reinterpret_cast<void*>(&AnInnocentFunction);
  write(STDOUT_FILENO, &ptr, sizeof(void*));
  fflush(stdout);

  raise(SIGTRAP);

  while (true) {
    if (Checksum() == safe) {
      AnInnocentFunction();
    } else {
      puts("Putting pepperni on pizza...");
    }
    fflush(stdout);
    raise(SIGTRAP);
  }
}