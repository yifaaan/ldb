cmake_minimum_required(VERSION 3.28)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_COMPILER clang++)
# set(CMAKE_ASM_COMPILER /usr/bin/gcc)
# set(CMAKE_ASM_COMPILER ${CMAKE_CXX_COMPILER})

project(ldb LANGUAGES CXX ASM)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


find_package(PkgConfig REQUIRED)
pkg_check_modules(readline REQUIRED IMPORTED_TARGET readline)

find_package(fmt CONFIG REQUIRED)
find_package(zydis CONFIG REQUIRED)
# find_package(spdlog CONFIG REQUIRED)

include(CTest)

add_subdirectory("src")
add_subdirectory("tools")

if(BUILD_TESTING)
    find_package(Catch2 CONFIG REQUIRED) 
    add_subdirectory("test") 
endif()