#include <sys/signal.h>
#include <unistd.h>

#include <cstdio>

int main() {
  unsigned long long a = 0xcafecafe;
  auto aAddr = &a;
  write(STDOUT_FILENO, &aAddr, sizeof(void*));
  fflush(stdout);
  raise(SIGTRAP);

  char b[12]{};
  auto bAddr = &b;
  write(STDOUT_FILENO, &bAddr, sizeof(void*));
  fflush(stdout);
  raise(SIGTRAP);
  printf("%s", b);
}