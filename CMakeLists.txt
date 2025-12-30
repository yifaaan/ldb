cmake_minimum_required(VERSION 3.28)
set(CMAKE_CXX_STANDARD 20)

project(ldb LANGUAGES CXX ASM)

# BUILD_TESTING default = ON
include(CTest)

# set(CMAKE_BUILD_WITH_INSTALL_RPATH ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


add_subdirectory("src")
add_subdirectory("tools")

if(BUILD_TESTING)
    add_subdirectory("test") 
endif()