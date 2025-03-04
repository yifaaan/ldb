#include <cstdio>
#include <numeric>

void AnInnocentFunction() { std::puts("Putting pineapple on pizza..."); }

void AnInnocentFunctionEnd() {}

int Checksum() {
  auto start = reinterpret_cast<volatile const char*>(&AnInnocentFunction);
  auto end = reinterpret_cast<volatile const char*>(&AnInnocentFunctionEnd);
  return std::accumulate(start, end, 0);
}