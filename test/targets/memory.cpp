#include <cstdio>
#include <sys/signal.h>
#include <unistd.h>

int main()
{
    unsigned long long a = 0xcafecafe;
    auto a_addresss = &a;

    write(STDOUT_FILENO, &a_addresss, sizeof(void*));
    fflush(stdout);
    raise(SIGTRAP);

    char b[12]{};
    auto b_address = &b;
    write(STDOUT_FILENO, &b_address, sizeof(void*));
    fflush(stdout);
    raise(SIGTRAP);

    printf("%s", b);
}