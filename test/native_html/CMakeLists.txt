# Tests for lib/Html (tokenizer + native parser). Depends only on lib/FictionBookCore.
add_executable(NativeHtmlTest
  NativeHtmlTest.cpp
  ${REPO_ROOT}/lib/Html/HtmlEntities.cpp
  ${REPO_ROOT}/lib/Html/HtmlTokenizer.cpp
  ${REPO_ROOT}/lib/Html/HtmlParser.cpp
)

target_include_directories(NativeHtmlTest PRIVATE
  ${REPO_ROOT}/lib/Html
  ${REPO_ROOT}/lib/FictionBookCore
  ${REPO_ROOT}/lib/ReaderWork
  ${REPO_ROOT}/src/reader/core/io
  ${CMAKE_CURRENT_SOURCE_DIR}/../common
)

target_link_libraries(NativeHtmlTest PRIVATE
  inkmod_test_common
  GTest::gtest_main
)

gtest_discover_tests(NativeHtmlTest)
