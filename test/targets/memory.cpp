#include <cstdio>
#include <sys/signal.h>
#include <unistd.h>

int main()
{
    unsigned long long a = 0xcafecafe;
    auto aAddress = &a;
    write(STDOUT_FILENO, &aAddress, sizeof(void*));
    fflush(stdout);
    raise(SIGTRAP);

    unsigned long long b = 0xaaaaaaaa;
    auto bAddress = &b;
    write(STDOUT_FILENO, &bAddress, sizeof(void*));
    fflush(stdout);
    raise(SIGTRAP);

    char c[12]{};
    auto cAddress = &c;
    write(STDOUT_FILENO, &cAddress, sizeof(void*));
    fflush(stdout);
    raise(SIGTRAP);

    printf("%s", c);
}