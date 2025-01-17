#include <cstdio>
#include <numeric>
#include <unistd.h>
#include <signal.h>

void AnInnocentFunction()
{
    std::puts("Putting pineapple on pizza...");
}

void AnInnocentFunctionEnd()
{}

int Checksum()
{
    auto start = reinterpret_cast<volatile const char*>(&AnInnocentFunction);
    auto end = reinterpret_cast<volatile const char*>(&AnInnocentFunctionEnd);
    return std::accumulate(start, end, 0);
}

int main()
{
    auto safe = Checksum();

    auto ptr = reinterpret_cast<void*>(&AnInnocentFunction);
    write(STDOUT_FILENO, &ptr, sizeof(void*));
    fflush(stdout);

    raise(SIGTRAP);

    while (true)
    {
        if (Checksum() == safe)
        {
            AnInnocentFunction();
        }
        else
        {
            std::puts("Putting pepperoni on pizza...");
        }

        fflush(stdout);
        raise(SIGTRAP);
    }
}