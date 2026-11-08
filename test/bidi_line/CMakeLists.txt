project(bidi_line_tests LANGUAGES C CXX)
add_executable(BidiLineTest
  BidiLineTest.cpp
  ${REPO_ROOT}/lib/Epub/Epub/BidiLine.cpp
  ${REPO_ROOT}/lib/MiniBidi/BidiUtils.cpp
  ${REPO_ROOT}/lib/TextShaping/TextShaping.cpp
  ${REPO_ROOT}/lib/TextShaping/HebrewShaper.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)
target_include_directories(BidiLineTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../minibidi_arabic/stubs  # no-op Logging.h
  ${REPO_ROOT}/lib/MiniBidi
  ${REPO_ROOT}/lib/TextShaping
  ${REPO_ROOT}/lib/Epub/Epub
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/Utf8
)
target_link_libraries(BidiLineTest PRIVATE wh_minibidi_c GTest::gtest_main)
gtest_discover_tests(BidiLineTest)
