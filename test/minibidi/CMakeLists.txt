project(wh_minibidi_tests LANGUAGES C CXX)
add_library(wh_minibidi_c STATIC ${REPO_ROOT}/lib/MiniBidi/minibidi.c)
target_include_directories(wh_minibidi_c PUBLIC ${REPO_ROOT}/lib/MiniBidi)

add_executable(BidiUtilsTest
  BidiUtilsTest.cpp
  ${REPO_ROOT}/lib/MiniBidi/BidiUtils.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)

target_include_directories(BidiUtilsTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../minibidi_arabic/stubs  # no-op Logging.h (real one needs Arduino)
  ${REPO_ROOT}/lib/MiniBidi
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/Utf8
)

target_link_libraries(BidiUtilsTest PRIVATE
  wh_minibidi_c
  GTest::gtest_main
)

gtest_discover_tests(BidiUtilsTest)

add_executable(BidiClassTest
  BidiClassTest.cpp
  ${REPO_ROOT}/lib/MiniBidi/BidiUtils.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)

target_include_directories(BidiClassTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../minibidi_arabic/stubs
  ${REPO_ROOT}/lib/MiniBidi
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/Utf8
)

target_link_libraries(BidiClassTest PRIVATE
  wh_minibidi_c
  GTest::gtest_main
)

gtest_discover_tests(BidiClassTest)
