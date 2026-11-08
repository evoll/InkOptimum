enable_language(C)  # Monocypher is plain C; the top-level project is CXX-only

add_executable(FirmwareSignatureTest
  FirmwareSignatureTest.cpp
  ${REPO_ROOT}/lib/FirmwareSignature/FirmwareSignature.cpp
  monocypher_core.c
  monocypher_ed25519.c)
target_include_directories(FirmwareSignatureTest PRIVATE
  ${REPO_ROOT}/lib/FirmwareSignature
  ${REPO_ROOT}/lib/Monocypher)
target_link_libraries(FirmwareSignatureTest PRIVATE GTest::gtest_main)
gtest_discover_tests(FirmwareSignatureTest)
