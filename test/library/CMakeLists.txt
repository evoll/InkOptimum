add_executable(LibraryTest LibraryTest.cpp)
target_include_directories(LibraryTest PRIVATE ${REPO_ROOT}/lib/Library)
target_link_libraries(LibraryTest PRIVATE inkmod_test_common GTest::gtest_main)
gtest_discover_tests(LibraryTest)
