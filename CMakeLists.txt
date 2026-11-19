cmake_minimum_required(VERSION 3.19)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS_DEBUG "-U_FORTIFY_SOURCE -O0 -g3")

project(simdb LANGUAGES CXX VERSION 4.0.0)
add_library(simdb INTERFACE)
add_library(SimDB::simdb ALIAS simdb)

find_package(SQLite3 3.19 REQUIRED)
message(STATUS "Using SQLite3 ${SQLite3_VERSION}")

find_package(ZLIB REQUIRED)
message(STATUS "Using zlib ${ZLIB_VERSION_STRING}")

find_package(Threads REQUIRED)

find_package(Boost REQUIRED)
message(STATUS "Using Boost ${Boost_VERSION}")

set(SIMDB_BASE ${CMAKE_CURRENT_SOURCE_DIR})
option(SIMDB_PEDANTIC "Enable build with -Wall -Wextra -Werror" ON)

if(SIMDB_PEDANTIC)
    target_compile_options(simdb INTERFACE
        $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra -Werror>
        $<$<CXX_COMPILER_ID:Clang>:-Wall -Wextra -Werror>
    )
endif()

option(SIMDB_COVERAGE "Instrument tests and examples for gcov code coverage (GCC only)" OFF)

if(SIMDB_COVERAGE)
    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        message(FATAL_ERROR "SIMDB_COVERAGE requires the GNU compiler")
    endif()
    # Atomic counter updates are required: SimDB is multi-threaded and plain
    # counters produce negative hit counts that corrupt the report.
    target_compile_options(simdb INTERFACE --coverage -fprofile-update=atomic -O0 -g)
    target_link_options(simdb INTERFACE --coverage)
endif()

target_include_directories(simdb INTERFACE
    $<BUILD_INTERFACE:${SIMDB_BASE}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(simdb INTERFACE
    SQLite::SQLite3
    ZLIB::ZLIB
    Threads::Threads
    Boost::headers
)

include(CTest)
add_custom_target(simdb_regress)
add_custom_command(
    TARGET simdb_regress
    POST_BUILD COMMAND ctest --output-on-failure)

macro(simdb_test target file)
    project(${target})
    add_executable(${target} ${file})
    add_test(NAME ${target}_test COMMAND $<TARGET_FILE:${target}>)
    add_dependencies(simdb_regress ${target})
    target_link_libraries(${target} PRIVATE simdb)
    target_include_directories(${target} PRIVATE ${SIMDB_BASE}/test ${SIMDB_BASE}/examples)
endmacro()

add_subdirectory(test EXCLUDE_FROM_ALL)
add_subdirectory(examples EXCLUDE_FROM_ALL)

include(CMakePackageConfigHelpers)

configure_package_config_file(
    cmake/SimDBConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/SimDBConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SimDB
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/SimDBConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

include(GNUInstallDirs)

# Install components:
#   simdb  - C++ headers and CMake package files
#   argos  - Argos viewer (python runtime + launcher)
# A plain "cmake --install" installs both. To install them to different
# prefixes, run "cmake --install <build> --component <name> --prefix <dir>".
install(
    FILES
        ${CMAKE_CURRENT_BINARY_DIR}/SimDBConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/SimDBConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SimDB
    COMPONENT simdb
)

install(
    DIRECTORY include/simdb
    DESTINATION include
    COMPONENT simdb
)

install(TARGETS simdb
    EXPORT SimDBTargets
    COMPONENT simdb
)

install(EXPORT SimDBTargets
    NAMESPACE SimDB::
    DESTINATION lib/cmake/SimDB
    COMPONENT simdb
)

install(
    DIRECTORY python/argos/
    DESTINATION bin/argos_runtime
    COMPONENT argos
    FILES_MATCHING
    PATTERN "*.py"
)
