add_executable(tests tests.cpp) 
target_link_libraries(tests PRIVATE ldb::libldb Catch2::Catch2WithMain)
