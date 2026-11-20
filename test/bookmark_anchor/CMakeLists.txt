add_executable(bookmark_anchor_test BookmarkAnchorTest.cpp)

target_include_directories(bookmark_anchor_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)

target_link_libraries(bookmark_anchor_test PRIVATE GTest::gtest_main)

gtest_discover_tests(bookmark_anchor_test)
