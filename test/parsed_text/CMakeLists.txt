add_executable(ParsedTextTest
  ParsedTextTest.cpp
  VerticalDrawStub.cpp
  ${REPO_ROOT}/lib/Epub/Epub/ParsedText.cpp
  ${REPO_ROOT}/lib/Epub/Epub/blocks/TextBlock.cpp
  ${REPO_ROOT}/lib/Epub/Epub/hyphenation/Hyphenator.cpp
  ${REPO_ROOT}/lib/Epub/Epub/hyphenation/LanguageRegistry.cpp
  ${REPO_ROOT}/lib/Epub/Epub/hyphenation/LiangHyphenation.cpp
  ${REPO_ROOT}/lib/Epub/Epub/hyphenation/HyphenationCommon.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${REPO_ROOT}/lib/CjkPunct/CjkPunct.cpp
)

# stubs/ comes first so the real GfxRenderer.h / BidiUtils.h are never seen.
target_include_directories(ParsedTextTest BEFORE PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
)
target_include_directories(ParsedTextTest PRIVATE
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Epub/Epub
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/lib/CjkBreak
  ${REPO_ROOT}/lib/CjkPunct
)

target_compile_definitions(ParsedTextTest PRIVATE
  HYPHENATION_RESOURCES_DIR="${REPO_ROOT}/test/hyphenation_eval/resources"
)

target_link_libraries(ParsedTextTest PRIVATE inkmod_test_common GTest::gtest_main)

gtest_discover_tests(ParsedTextTest)
