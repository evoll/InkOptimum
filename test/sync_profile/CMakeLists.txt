add_executable(SyncProfileTest SyncProfileTest.cpp)
target_include_directories(SyncProfileTest PRIVATE ${REPO_ROOT}/lib/SyncProfile)
target_link_libraries(SyncProfileTest PRIVATE inkmod_test_common GTest::gtest_main)
gtest_discover_tests(SyncProfileTest)
