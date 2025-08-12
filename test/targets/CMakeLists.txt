add_executable(RunEndlessly RunEndlessly.cpp)
add_executable(EndImmediately EndImmediately.cpp)

add_executable(RegWrite RegWrite.s)
target_compile_options(RegWrite PRIVATE -pie)