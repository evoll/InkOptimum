enable_language(C)  # the top-level project is CXX-only; uzlib is plain C
get_filename_component(ZIP_TEST_ROOT ${REPO_ROOT} ABSOLUTE)
add_executable(ZipFileTest ZipFileTest.cpp
  ${ZIP_TEST_ROOT}/lib/ZipFile/ZipFile.cpp
  ${ZIP_TEST_ROOT}/lib/InflateReader/InflateReader.cpp
  uzlib_unit.c)
# The shims come first so that <Arduino.h>, <HalStorage.h>, <Logging.h> and <Print.h> resolve to the host stand-ins.
target_include_directories(ZipFileTest BEFORE PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/shims
  ${ZIP_TEST_ROOT}/lib/ZipFile
  ${ZIP_TEST_ROOT}/lib/InflateReader
  ${ZIP_TEST_ROOT}/lib/ReaderWork
  ${ZIP_TEST_ROOT}/lib/uzlib/src)
target_link_libraries(ZipFileTest PRIVATE inkmod_test_common GTest::gtest_main)
gtest_discover_tests(ZipFileTest)
