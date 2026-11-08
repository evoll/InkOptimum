# Tests for lib/Rtf (RTF -> HTML converter), including the full RTF -> HtmlParser
# path. Depends on lib/FictionBookCore and lib/Html only.
add_executable(NativeRtfTest
  NativeRtfTest.cpp
  ${REPO_ROOT}/lib/Rtf/RtfConverter.cpp
  ${REPO_ROOT}/lib/FictionBookCore/Codepage.cpp
  ${REPO_ROOT}/lib/Html/HtmlEntities.cpp
  ${REPO_ROOT}/lib/Html/HtmlTokenizer.cpp
  ${REPO_ROOT}/lib/Html/HtmlParser.cpp
)

target_include_directories(NativeRtfTest PRIVATE
  ${REPO_ROOT}/lib/Rtf
  ${REPO_ROOT}/lib/Html
  ${REPO_ROOT}/lib/FictionBookCore
  ${REPO_ROOT}/lib/ReaderWork
  ${REPO_ROOT}/src/reader/core/io
  ${CMAKE_CURRENT_SOURCE_DIR}/../common
)

target_link_libraries(NativeRtfTest PRIVATE
  inkmod_test_common
  GTest::gtest_main
)

gtest_discover_tests(NativeRtfTest)
