# Tests for lib/Mobi (MOBI / AZW3 reader), including the full MOBI -> HtmlParser
# path. Depends on lib/FictionBookCore and lib/Html only.
add_executable(NativeMobiTest
  NativeMobiTest.cpp
  ${REPO_ROOT}/lib/Mobi/MobiBook.cpp
  ${REPO_ROOT}/lib/Mobi/MobiConvert.cpp
  ${REPO_ROOT}/lib/Mobi/MobiIndex.cpp
  ${REPO_ROOT}/lib/Mobi/HuffCdic.cpp
  ${REPO_ROOT}/lib/Mobi/PalmDoc.cpp
  ${REPO_ROOT}/lib/FictionBookCore/Codepage.cpp
  ${REPO_ROOT}/lib/Html/HtmlEntities.cpp
  ${REPO_ROOT}/lib/Html/HtmlTokenizer.cpp
  ${REPO_ROOT}/lib/Html/HtmlParser.cpp
)

target_include_directories(NativeMobiTest PRIVATE
  ${REPO_ROOT}/lib/Mobi
  ${REPO_ROOT}/lib/Html
  ${REPO_ROOT}/lib/FictionBookCore
  ${REPO_ROOT}/lib/ReaderWork
  ${REPO_ROOT}/src/reader/core/io
  ${CMAKE_CURRENT_SOURCE_DIR}/../common
)

target_link_libraries(NativeMobiTest PRIVATE
  inkmod_test_common
  GTest::gtest_main
)

gtest_discover_tests(NativeMobiTest)
