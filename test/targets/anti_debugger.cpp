#include <unistd.h>

#include <csignal>
#include <cstdio>
#include <numeric>
void an_innocent_function()
{
    std::puts("Putting pineapple on pizza...");
}

void an_innocent_function_end()
{
}

int check_sum()
{
    auto start = reinterpret_cast<volatile const char*>(&an_innocent_function);
    auto end = reinterpret_cast<volatile const char*>(&an_innocent_function_end);
    return std::accumulate(start, end, 0);
}

int main()
{
    auto safe = check_sum();
    auto func_ptr = reinterpret_cast<void*>(&an_innocent_function);
    write(STDOUT_FILENO, &func_ptr, sizeof(void*));
    fflush(stdout);
    raise(SIGTRAP);

    while (true)
    {
        auto current = check_sum();
        if (current != safe)
        {
            std::puts("Someone is trying to debug me!");
        }
        else
        {
            an_innocent_function();
        }
        fflush(stdout);
        raise(SIGTRAP);
    }
}
