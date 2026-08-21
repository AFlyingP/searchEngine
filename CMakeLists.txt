cmake_minimum_required(VERSION 3.20)
project(needlefish VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for clangd/clang-tidy
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Options
option(ENABLE_SANITIZERS "Enable ASan and UBSan" OFF)
option(ENABLE_TSAN "Enable ThreadSanitizer" OFF)
option(ENABLE_LIBFUZZER "Enable libFuzzer instrumentation" OFF)
option(ENABLE_LTO "Enable Link-Time Optimization" OFF)
option(ENABLE_COVERAGE "Enable code coverage instrumentation" OFF)
option(ENABLE_NATIVE_ARCH "Enable native architecture instructions" OFF)
option(BUILD_TESTS "Build test suite" ON)
option(BUILD_BENCHMARKS "Build benchmark suite" ON)
option(BUILD_FUZZERS "Build fuzz targets" ON)

# Third-party zlib for HTTP gzip compression
include(FetchContent)
set(FETCHCONTENT_QUIET ON)
set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    zlib
    URL https://github.com/madler/zlib/archive/refs/tags/v1.3.1.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(zlib)

# Core library
add_library(needlefish_core
    src/bitvector/bitvector.cpp
    src/wavelet/wavelet_tree.cpp
    src/sa/sais.cpp
    src/fm/fm_index.cpp
    src/util/utf8.cpp
    src/util/porter_stemmer.cpp
    src/util/analyzer.cpp
    src/invidx/compression.cpp
    src/invidx/postings.cpp
    src/invidx/radix_trie.cpp
    src/invidx/index_builder.cpp
    src/store/mmap.cpp
    src/store/index_file.cpp
    src/rank/wand.cpp
    src/rank/query_eval.cpp
    src/rank/snippet.cpp
    src/rank/hybrid_search.cpp
    src/automata/regex.cpp
    src/automata/levenshtein.cpp
    src/automata/autocomplete.cpp
    src/server/http_server.cpp
)

target_link_libraries(needlefish_core PRIVATE $<BUILD_INTERFACE:zlibstatic>)

if (WIN32)
    target_link_libraries(needlefish_core PUBLIC ws2_32)
endif()

target_include_directories(needlefish_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# C API Shared Library
add_library(needlefish_c SHARED
    src/c_api/needlefish_c.cpp
)
target_link_libraries(needlefish_c PRIVATE needlefish_core)
target_include_directories(needlefish_c
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_compile_definitions(needlefish_c PRIVATE NEEDLEFISH_EXPORTS)

# Strict warning flags
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(needlefish_core PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Werror
        -Wconversion
        -Wsign-conversion
        -Wshadow
        -Wnon-virtual-dtor
    )
    if (ENABLE_NATIVE_ARCH)
        target_compile_options(needlefish_core PUBLIC -march=native)
    endif()
    if (ENABLE_SANITIZERS)
        target_compile_options(needlefish_core PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(needlefish_core PUBLIC -fsanitize=address,undefined)
    endif()
    if (ENABLE_TSAN)
        target_compile_options(needlefish_core PUBLIC -fsanitize=thread -fno-omit-frame-pointer)
        target_link_options(needlefish_core PUBLIC -fsanitize=thread)
    endif()
    if (ENABLE_COVERAGE)
        target_compile_options(needlefish_core PUBLIC -fprofile-instr-generate -fcoverage-mapping)
        target_link_options(needlefish_core PUBLIC -fprofile-instr-generate -fcoverage-mapping)
    endif()
elseif (MSVC)
    target_compile_options(needlefish_core PRIVATE /W4 /WX /permissive-)
endif()

if (ENABLE_LTO)
    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(needlefish_core PUBLIC -flto=thin)
        target_link_options(needlefish_core PUBLIC -flto=thin)
    elseif (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        target_compile_options(needlefish_core PUBLIC -flto)
        target_link_options(needlefish_core PUBLIC -flto)
    elseif (MSVC)
        set_property(TARGET needlefish_core PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()
endif()

# CLI Executable
add_executable(needlefish_cli
    cli/main.cpp
)
target_link_libraries(needlefish_cli PRIVATE needlefish_core)
set_target_properties(needlefish_cli PROPERTIES OUTPUT_NAME "needlefish")

# Install rules & CMake Package Config
include(GNUInstallDirs)

install(TARGETS needlefish_core needlefish_c needlefish_cli
    EXPORT NeedlefishTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY src/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/needlefish
    FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h"
)

install(EXPORT NeedlefishTargets
    FILE NeedlefishTargets.cmake
    NAMESPACE needlefish::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Needlefish
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/NeedlefishConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/NeedlefishConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Needlefish
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/NeedlefishConfigVersion.cmake
    VERSION 1.0.0
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/NeedlefishConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/NeedlefishConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Needlefish
)

# Fuzzers
if (BUILD_FUZZERS)
    add_subdirectory(fuzz)
endif()

# Third-party dependencies for tests & benchmarks
if (BUILD_TESTS OR BUILD_BENCHMARKS)
    include(FetchContent)
    set(FETCHCONTENT_QUIET ON)

    if (BUILD_TESTS)
        FetchContent_Declare(
            googletest
            URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        )
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)
        enable_testing()
        add_subdirectory(tests)
    endif()

    if (BUILD_BENCHMARKS)
        set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)
        set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
        set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(
            googlebenchmark
            URL https://github.com/google/benchmark/archive/refs/tags/v1.8.3.zip
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        )
        FetchContent_MakeAvailable(googlebenchmark)
        add_subdirectory(bench)
    endif()
endif()
