add_executable(cover_fallbacks_test
  CoverFallbacksTest.cpp
  ../../lib/FsHelpers/FsHelpers.cpp
)

target_include_directories(cover_fallbacks_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../cover_ref_scanner
  ${CMAKE_CURRENT_SOURCE_DIR}/../fs_helpers
  ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/FsHelpers
  ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/Epub/Epub
)

target_link_libraries(cover_fallbacks_test PRIVATE
  GTest::gtest_main
  inkmod_test_common
)

gtest_discover_tests(cover_fallbacks_test)
