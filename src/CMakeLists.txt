add_library(libldb process.cpp pipe.cpp registers.cpp breakpoint_site.cpp disassembler.cpp)
add_library(ldb::libldb ALIAS libldb)

target_link_libraries(libldb PRIVATE Zydis::Zydis)
set_target_properties(
    libldb
    PROPERTIES OUTPUT_NAME ldb
)

target_compile_features(libldb PUBLIC cxx_std_17)

target_include_directories(
    libldb
    PUBLIC
        $<INSTALL_INTERFACE:include>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/include
)

include(GNUInstallDirs) 
install(
    TARGETS libldb 
    EXPORT ldb-targets 
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} 
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} 
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} 
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} 
)

# install the public headers
install(
    DIRECTORY ${PROJECT_SOURCE_DIR}/include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
# make libldb importable by other CMake projects is save its export information to a file
install(
    EXPORT ldb-targets
    FILE ldb-config.cmake
    NAMESPACE ldb::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ldb
)