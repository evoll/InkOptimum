add_executable(buffered_writer_test BufferedWriterTest.cpp)

target_include_directories(buffered_writer_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/BufferedWriter
)

target_link_libraries(buffered_writer_test PRIVATE GTest::gtest_main)

gtest_discover_tests(buffered_writer_test)
