add_executable(ldb ldb.cpp) 
target_link_libraries(ldb PRIVATE ldb::libldb PkgConfig::readline fmt::fmt)

include(GNUInstallDirs)
install(
    TARGETS ldb
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)