cmake_minimum_required(VERSION 3.25)
project(glaze-sql VERSION 1.0.0 LANGUAGES CXX)

option(ENABLE_TESTS "Enable tests" ON)

## add stdc++ compiler flag
include(CheckCXXCompilerFlag)
CHECK_CXX_COMPILER_FLAG("-std=c++26" COMPILER_SUPPORTS_CXX26)
CHECK_CXX_COMPILER_FLAG("-std=c++23" COMPILER_SUPPORTS_CXX23)
CHECK_CXX_COMPILER_FLAG("-std=c++20" COMPILER_SUPPORTS_CXX20)
if(COMPILER_SUPPORTS_CXX26)
    set(STD_CPP cxx_std_26)
elseif(COMPILER_SUPPORTS_CXX23)
    set(STD_CPP cxx_std_23)
elseif(COMPILER_SUPPORTS_CXX20)
    set(STD_CPP cxx_std_20)
else()
    message(STATUS "The compiler ${CMAKE_CXX_COMPILER} has no C++20 support. Please use a different C++ compiler.")
endif()

## add warning
if (CMAKE_COMPILER_IS_GNUCXX)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pedantic")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g3")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-omit-frame-pointer")
endif()

if (MSVC)
    if(CMAKE_CXX_FLAGS MATCHES "/W[0-4]")
        string(REGEX REPLACE "/W[0-4]" "/W4" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    else()
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4")
    endif()
endif()

## optimization
if (CMAKE_COMPILER_IS_GNUCXX)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=native")
endif()
if (MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /Ox")
endif()


## static option
option(STATIC "static binary" OFF)
if (CMAKE_COMPILER_IS_GNUCXX)
    if(STATIC)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static -static-libstdc++ -static-libgcc")
    endif()
endif()

## add LTO flag
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported OUTPUT error)
if(ipo_supported)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

## other flag
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

## google perf tools
# HEAPPROFILE=heap CPUPROFILE=test.prof xxx
# pprof --text xxx yyy.prof
#set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -lprofiler -ltcmalloc")

find_package(glaze REQUIRED CONFIG)
find_package(SQLite3 REQUIRED)

add_library(glz-sql INTERFACE)
add_library(glz-sql::glz-sql ALIAS glz-sql)

target_include_directories(glz-sql INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(glz-sql INTERFACE ${STD_CPP})

target_link_libraries(glz-sql INTERFACE
    glaze::glaze
    SQLite3::SQLite3
)

if(ENABLE_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS glz-sql
    EXPORT glz-sql-targets
)

install(DIRECTORY include/glz-sql
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT glz-sql-targets
    FILE glz-sql-targets.cmake
    NAMESPACE glz-sql::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/glz-sql
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/glz-sql-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/glz-sql-config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/glz-sql
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/glz-sql-config-version.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/glz-sql-config.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/glz-sql-config-version.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/glz-sql
)
