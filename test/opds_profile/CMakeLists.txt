add_executable(OpdsProfileTest OpdsProfileTest.cpp)
target_include_directories(OpdsProfileTest PRIVATE ${REPO_ROOT}/lib/OpdsProfile)
target_link_libraries(OpdsProfileTest PRIVATE inkmod_test_common GTest::gtest_main)
gtest_discover_tests(OpdsProfileTest)
