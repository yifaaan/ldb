add_library(libldb libldb.cpp)
add_library(ldb::libldb ALIAS libldb)

set_target_properties(libldb PROPERTIES OUTPUT_NAME ldb)
target_compile_features(libldb PUBLIC cxx_std_20)
target_include_directories(libldb
    PRIVATE include
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)