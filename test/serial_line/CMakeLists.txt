add_executable(serial_line_test SerialLineBufferTest.cpp)

target_include_directories(serial_line_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)

target_link_libraries(serial_line_test PRIVATE GTest::gtest_main)

gtest_discover_tests(serial_line_test)
