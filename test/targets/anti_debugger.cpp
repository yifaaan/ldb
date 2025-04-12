#include <unistd.h>
#include <signal.h>

#include <cstdio>
#include <numeric>

void AnInnocentFunction()
{
	std::puts("Putting pineapple on pizza...");
}

void AnInnocentFunctionEnd() {}

int CheckSum()
{
	auto start = reinterpret_cast<volatile const char*>(&AnInnocentFunction);
	auto end = reinterpret_cast<volatile const char*>(&AnInnocentFunctionEnd);
	return std::accumulate(start, end, 0);
}
// 55c752a5e000-55c752a5f000 r-xp 00001000 08:20 96913

int main()
{
	auto safe = CheckSum();
	auto ptr = reinterpret_cast<void*>(&AnInnocentFunction);
	write(STDOUT_FILENO, &ptr, sizeof(void*));
	fflush(stdout);
	raise(SIGTRAP);
	while (true)
	{
		sleep(1);
		if (CheckSum() == safe)
		{
			AnInnocentFunction();
		}
		else
		{
			puts("Putting pepperoni on pizza...");
		}
		fflush(stdout);
		raise(SIGTRAP);
	}
}