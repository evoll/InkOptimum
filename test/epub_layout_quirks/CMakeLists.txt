add_executable(epub_layout_quirks_test EpubLayoutQuirksTest.cpp)

target_include_directories(epub_layout_quirks_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/Epub/Epub
)

target_link_libraries(epub_layout_quirks_test PRIVATE GTest::gtest_main)

gtest_discover_tests(epub_layout_quirks_test)
