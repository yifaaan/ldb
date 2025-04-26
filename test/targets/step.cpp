#include <cstdio>

__attribute__((always_inline)) inline void ScratchEars() {
  int a = 1;
  std::puts("Scratching ears");
}

__attribute__((always_inline)) inline void PetCat() {
  int a = 1;
  ScratchEars();
  std::puts("Done petting cat");
}

void FindHappiness() {
  int a = 1;
  PetCat();
  std::puts("Found happiness");
}

int main() {
  FindHappiness();
  FindHappiness();
}