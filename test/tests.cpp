#include <elf.h>
#include <fcntl.h>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <signal.h>
#include <sys/types.h>

#include <catch2/catch_test_macros.hpp>
#include <format>
#include <fstream>
#include <iostream>

#include <regex>

TEST_CASE("validate environment")
{
	REQUIRE(false);
}
