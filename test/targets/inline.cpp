#include <cstdio>

__attribute__((always_inline)) inline void a() { std::puts("Hello"); }

__attribute__((always_inline)) inline void b() { a(); }

int main() { b(); }