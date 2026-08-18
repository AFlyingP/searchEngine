cmake_minimum_required(VERSION 3.20)
project(needlefish VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for clangd/clang-tidy
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Options
option(ENABLE_SANITIZERS "Enable ASan and UBSan" OFF)
option(ENABLE_TSAN "Enable ThreadSanitizer" OFF)
option(ENABLE_LTO "Enable Link-Time Optimization" OFF)
option(ENABLE_COVERAGE "Enable code coverage instrumentation" OFF)
option(ENABLE_NATIVE_ARCH "Enable native architecture instructions" OFF)
option(BUILD_TESTS "Build test suite" ON)
option(BUILD_BENCHMARKS "Build benchmark suite" ON)

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
)

target_include_directories(needlefish_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:include>
)

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

# CLI Executable placeholder
add_executable(needlefish_cli
    cli/main.cpp
)
target_link_libraries(needlefish_cli PRIVATE needlefish_core)
set_target_properties(needlefish_cli PROPERTIES OUTPUT_NAME "needlefish")

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
        # Suppress warnings in googletest
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
