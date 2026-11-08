add_executable(png_bit_depth_test PngBitDepthTest.cpp)

target_include_directories(png_bit_depth_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/PngToBmpConverter
)

target_link_libraries(png_bit_depth_test PRIVATE GTest::gtest_main)

gtest_discover_tests(png_bit_depth_test)
