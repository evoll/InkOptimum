# Tests for lib/GifDecoder (platform-free LZW decoder).
add_executable(NativeGifTest
  NativeGifTest.cpp
  ${REPO_ROOT}/lib/GifDecoder/GifDecoder.cpp
)

target_include_directories(NativeGifTest PRIVATE
  ${REPO_ROOT}/lib/GifDecoder
)

target_link_libraries(NativeGifTest PRIVATE
  inkmod_test_common
  GTest::gtest_main
)

gtest_discover_tests(NativeGifTest)
