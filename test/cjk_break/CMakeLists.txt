add_executable(CjkBreakTest
  CjkBreakTest.cpp
  ${REPO_ROOT}/lib/Epub/Epub/hyphenation/Hyphenator.cpp
  ${REPO_ROOT}/lib/Epub/Epub/hyphenation/LanguageRegistry.cpp
  ${REPO_ROOT}/lib/Epub/Epub/hyphenation/LiangHyphenation.cpp
  ${REPO_ROOT}/lib/Epub/Epub/hyphenation/HyphenationCommon.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)

target_include_directories(CjkBreakTest PRIVATE
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Epub/Epub
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/CjkBreak
)

target_compile_definitions(CjkBreakTest PRIVATE
  HYPHENATION_RESOURCES_DIR="${REPO_ROOT}/test/hyphenation_eval/resources"
)

target_link_libraries(CjkBreakTest PRIVATE inkmod_test_common GTest::gtest_main)

gtest_discover_tests(CjkBreakTest)
