# Fuzz targets for Needlefish Search Engine

set(FUZZ_TARGETS
    fuzz_query_parser
    fuzz_analyzer
    fuzz_index_loader
    fuzz_levenshtein
)

foreach(target ${FUZZ_TARGETS})
    add_executable(${target} ${target}.cpp)
    target_link_libraries(${target} PRIVATE needlefish_core)
    target_include_directories(${target} PRIVATE ${CMAKE_SOURCE_DIR}/src)

    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND ENABLE_LIBFUZZER)
        target_compile_options(${target} PRIVATE -fsanitize=fuzzer,address,undefined)
        target_link_options(${target} PRIVATE -fsanitize=fuzzer,address,undefined)
        target_compile_definitions(${target} PRIVATE NEEDLEFISH_LIBFUZZER=1)
    endif()
endforeach()
