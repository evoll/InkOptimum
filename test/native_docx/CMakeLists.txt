# Tests for lib/Docx (package reduction + native parser). The library depends only
# on lib/FictionBookCore: no FB2/FB3 code and no expat.
add_executable(NativeDocxTest
  NativeDocxTest.cpp
  ${REPO_ROOT}/lib/Docx/DocxInfo.cpp
  ${REPO_ROOT}/lib/Docx/DocxPackage.cpp
  ${REPO_ROOT}/lib/Docx/DocxParser.cpp
  ${REPO_ROOT}/lib/FictionBookCore/OpcPackage.cpp
  ${REPO_ROOT}/lib/FictionBookCore/ZipReader.cpp
  ${REPO_ROOT}/lib/FictionBookCore/Inflate.cpp
)

target_include_directories(NativeDocxTest PRIVATE
  ${REPO_ROOT}/lib/Docx
  ${REPO_ROOT}/lib/FictionBookCore
  ${REPO_ROOT}/lib/ReaderWork
  ${REPO_ROOT}/src/reader/core/io
  ${CMAKE_CURRENT_SOURCE_DIR}/../common
)

target_link_libraries(NativeDocxTest PRIVATE
  inkmod_test_common
  GTest::gtest_main
)

gtest_discover_tests(NativeDocxTest)
