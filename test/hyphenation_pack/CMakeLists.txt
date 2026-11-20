add_executable(HyphenationPackTest
  HyphenationPackTest.cpp
  ${REPO_ROOT}/lib/Epub/Epub/hyphenation/LanguageRegistry.cpp
  ${REPO_ROOT}/lib/Epub/Epub/hyphenation/LiangHyphenation.cpp
  ${REPO_ROOT}/lib/Epub/Epub/hyphenation/HyphenationCommon.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)
target_include_directories(HyphenationPackTest PRIVATE
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Epub/Epub/hyphenation
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/CjkBreak
)
target_link_libraries(HyphenationPackTest PRIVATE inkmod_test_common GTest::gtest_main)
gtest_discover_tests(HyphenationPackTest)
