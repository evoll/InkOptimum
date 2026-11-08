add_executable(static_etag_test StaticEtagTest.cpp)

target_include_directories(static_etag_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src/network
)

target_link_libraries(static_etag_test PRIVATE GTest::gtest_main)

gtest_discover_tests(static_etag_test)
