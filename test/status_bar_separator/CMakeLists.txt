add_executable(status_bar_separator_test StatusBarSeparatorTest.cpp)

target_include_directories(status_bar_separator_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src/util
)

target_link_libraries(status_bar_separator_test PRIVATE GTest::gtest_main)

gtest_discover_tests(status_bar_separator_test)
