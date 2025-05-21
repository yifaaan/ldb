add_library(libldb libldb.cpp process.cpp pipe.cpp registers.cpp breakpoint_site.cpp disassembler.cpp watchpoint.cpp syscalls.cpp elf.cpp types.cpp target.cpp dwarf.cpp)
add_library(ldb::libldb ALIAS libldb)

set_target_properties(
    libldb 
    PROPERTIES OUTPUT_NAME ldb
)
target_link_libraries(libldb PRIVATE Zydis::Zydis)

target_compile_features(libldb PUBLIC cxx_std_20)

target_include_directories(
    libldb
    PRIVATE ${CMAKE_SOURCE_DIR}/src/include
    PUBLIC
     $<INSTALL_INTERFACE:include>
     $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
)

include(GNUInstallDirs)
install(TARGETS libldb
    EXPORT ldb-targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(
    DIRECTORY ${CMAKE_SOURCE_DIR}/include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(
    EXPORT ldb-targets
    FILE ldb-config.cmake
    NAMESPACE ldb::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ldb
)
