# Benchmarks CMakeLists.txt

add_executable(bench_needlefish
    bench_main.cpp
)
target_link_libraries(bench_needlefish PRIVATE needlefish_core benchmark::benchmark)

add_executable(bench_matrix
    bench_matrix.cpp
)
target_link_libraries(bench_matrix PRIVATE needlefish_core)
