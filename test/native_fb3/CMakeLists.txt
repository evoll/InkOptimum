# Tests for lib/Fb3 (OPC package layer + native parser). The FB3 library
# depends only on lib/FictionBookCore: no FB2 code and no expat.
add_executable(NativeFb3Test
  NativeFb3Test.cpp
  ${REPO_ROOT}/lib/Fb3/Fb3Package.cpp
  ${REPO_ROOT}/lib/Fb3/Fb3Parser.cpp
  ${REPO_ROOT}/lib/FictionBookCore/OpcPackage.cpp
  ${REPO_ROOT}/lib/FictionBookCore/ZipReader.cpp
  ${REPO_ROOT}/lib/FictionBookCore/Inflate.cpp
)

target_include_directories(NativeFb3Test PRIVATE
  ${REPO_ROOT}/lib/Fb3
  ${REPO_ROOT}/lib/FictionBookCore
  ${REPO_ROOT}/lib/ReaderWork
  ${REPO_ROOT}/src/reader/core/io
)

target_link_libraries(NativeFb3Test PRIVATE
  inkmod_test_common
  GTest::gtest_main
)

gtest_discover_tests(NativeFb3Test)
