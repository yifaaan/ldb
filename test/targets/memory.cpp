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
}