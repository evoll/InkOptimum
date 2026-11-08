add_executable(toc_fallback_test TocFallbackPolicyTest.cpp ImageEntryNameTest.cpp)

target_include_directories(toc_fallback_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/Epub/Epub
)

target_link_libraries(toc_fallback_test PRIVATE GTest::gtest_main)

gtest_discover_tests(toc_fallback_test)
