add_executable(RunEndlessly RunEndlessly.cpp)
add_executable(EndImmediately EndImmediately.cpp)

add_executable(RegWrite RegWrite.s)
target_compile_options(RegWrite PRIVATE -pie)

add_executable(RegRead RegRead.s)
target_compile_options(RegRead PRIVATE -pie)