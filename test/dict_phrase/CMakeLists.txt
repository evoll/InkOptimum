add_executable(DictPhraseTest DictPhraseTest.cpp)
target_include_directories(DictPhraseTest PRIVATE ${REPO_ROOT}/lib/DictPhrase)
target_link_libraries(DictPhraseTest PRIVATE inkmod_test_common GTest::gtest_main)
gtest_discover_tests(DictPhraseTest)
