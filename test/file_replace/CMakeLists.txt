add_executable(file_replace_test FileReplaceTest.cpp)

target_include_directories(file_replace_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/FileReplace
)

target_link_libraries(file_replace_test PRIVATE GTest::gtest_main)

gtest_discover_tests(file_replace_test)
