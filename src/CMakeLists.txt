add_library(libldb libldb.cpp process.cpp pipe.cpp registers.cpp breakpoint_site.cpp disassembler.cpp watchpoint.cpp syscalls.cpp elf.cpp types.cpp)
add_library(ldb::libldb ALIAS libldb)

set_target_properties(libldb PROPERTIES OUTPUT_NAME ldb)

target_compile_features(libldb PUBLIC cxx_std_20)

target_link_libraries(libldb PRIVATE Zydis::Zydis spdlog::spdlog)

target_include_directories(
    libldb 
    PUBLIC
        $<INSTALL_INTERFACE:include>
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src/include
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
install(
    DIRECTORY ${PROJECT_SOURCE_DIR}/include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(
    EXPORT ldb-targets
    FILE ldb-config.cmake
    NAMESPACE ldb::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ldb
    
)
message("src/{CMAKE_SOURCE_DIR}: ${CMAKE_SOURCE_DIR}")
message("src/{CMAKE_CURRENT_SOURCE_DIR}: ${CMAKE_CURRENT_SOURCE_DIR}")
message("src/{PROJECT_SOURCE_DIR}: ${PROJECT_SOURCE_DIR}")