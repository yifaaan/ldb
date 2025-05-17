#include <sys/signal.h>
#include <unistd.h>

#include <cstdio>

int main()
{
    // for read
    unsigned long long a = 0xcafecafe;
    auto a_address = &a;
    write(STDOUT_FILENO, &a_address, sizeof(void*));
    fflush(stdout);
    raise(SIGTRAP);

    // for write
    char b[12]{};
    auto b_address = &b;
    write(STDOUT_FILENO, &b_address, sizeof(void*));
    fflush(stdout);
    raise(SIGTRAP);
    printf("%s", b);
    fflush(stdout);
}
