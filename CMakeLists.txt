cmake_minimum_required(VERSION 3.19)
project(ldb LANGUAGES CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS true)

include(CTest)

add_subdirectory("src")
add_subdirectory("tools")

# if(BUILD_TESTING)
#     add_subdirectory("test")
# endif()