# Tests for lib/Markdown. The library depends only on lib/FictionBookCore
# (headers: parser contract and sink types).
add_executable(NativeMarkdownTest
  NativeMarkdownTest.cpp
  ${REPO_ROOT}/lib/Markdown/MarkdownParser.cpp
)

target_include_directories(NativeMarkdownTest PRIVATE
  ${REPO_ROOT}/lib/Markdown
  ${REPO_ROOT}/lib/FictionBookCore
  ${REPO_ROOT}/lib/ReaderWork
  ${REPO_ROOT}/src/reader/core/io
)

target_link_libraries(NativeMarkdownTest PRIVATE
  inkmod_test_common
  GTest::gtest_main
)

gtest_discover_tests(NativeMarkdownTest)
