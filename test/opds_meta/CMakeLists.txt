add_executable(OpdsMetaTest OpdsMetaTest.cpp)
target_include_directories(OpdsMetaTest PRIVATE ${REPO_ROOT}/lib/OpdsMeta)
target_link_libraries(OpdsMetaTest PRIVATE inkmod_test_common GTest::gtest_main)
gtest_discover_tests(OpdsMetaTest)
