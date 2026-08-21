# Tests CMakeLists.txt

add_executable(test_needlefish_unit
    unit/test_bitvector.cpp
    unit/test_wavelet.cpp
    unit/test_sais.cpp
    unit/test_fm_index.cpp
    unit/test_analyzer.cpp
    unit/test_postings.cpp
    unit/test_radix_trie.cpp
    unit/test_query_eval.cpp
    unit/test_golden_queries.cpp
    unit/test_automata.cpp
    unit/test_server.cpp
    unit/test_concurrency.cpp
    unit/test_edge_cases.cpp
    unit/test_review_regressions.cpp
    integration/test_cli_e2e.cpp
)
target_link_libraries(test_needlefish_unit PRIVATE needlefish_core needlefish_c GTest::gtest_main)
add_test(NAME test_unit COMMAND test_needlefish_unit)

add_executable(test_needlefish_property
    property/prop_bitvector.cpp
    property/prop_wavelet.cpp
    property/prop_sais.cpp
    property/prop_fm_index.cpp
    property/prop_invidx.cpp
    property/prop_automata.cpp
)
target_link_libraries(test_needlefish_property PRIVATE needlefish_core GTest::gtest_main)
add_test(NAME test_property COMMAND test_needlefish_property)
