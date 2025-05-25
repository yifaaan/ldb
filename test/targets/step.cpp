#include <cstdio>

__attribute__((always_inline)) inline void ScratchEars() {
  std::puts("Scratching ears");
}

__attribute__((always_inline)) inline void PetCat() {
  ScratchEars();
  std::puts("Done petting cat");
}

void FindHappiness() {
  PetCat();
  std::puts("Found happiness");
}

int main() {
  FindHappiness();
  FindHappiness();
}