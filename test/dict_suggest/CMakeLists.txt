add_executable(DictSuggestTest DictSuggestTest.cpp)
target_include_directories(DictSuggestTest PRIVATE ${REPO_ROOT}/lib/DictSuggest)
target_link_libraries(DictSuggestTest PRIVATE inkmod_test_common GTest::gtest_main)
gtest_discover_tests(DictSuggestTest)
