add_executable(shelves_test ShelvesTest.cpp ${REPO_ROOT}/lib/Shelves/Shelves.cpp)
target_include_directories(shelves_test PRIVATE ${REPO_ROOT}/lib/Shelves)
target_link_libraries(shelves_test PRIVATE GTest::gtest_main)
gtest_discover_tests(shelves_test)
