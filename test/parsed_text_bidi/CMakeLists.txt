project(parsed_text_bidi_tests LANGUAGES C CXX)
add_executable(ParsedTextBidiTest
  ParsedTextBidiTest.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../parsed_text/VerticalDrawStub.cpp
  ${REPO_ROOT}/lib/Epub/Epub/ParsedText.cpp
  ${REPO_ROOT}/lib/Epub/Epub/blocks/TextBlock.cpp
  ${REPO_ROOT}/lib/Epub/Epub/hyphenation/Hyphenator.cpp
  ${REPO_ROOT}/lib/Epub/Epub/hyphenation/LanguageRegistry.cpp
  ${REPO_ROOT}/lib/Epub/Epub/hyphenation/LiangHyphenation.cpp
  ${REPO_ROOT}/lib/Epub/Epub/hyphenation/HyphenationCommon.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${REPO_ROOT}/lib/CjkPunct/CjkPunct.cpp
  ${REPO_ROOT}/lib/Epub/Epub/BidiLine.cpp
  ${REPO_ROOT}/lib/MiniBidi/BidiUtils.cpp
  ${REPO_ROOT}/lib/TextShaping/TextShaping.cpp
  ${REPO_ROOT}/lib/TextShaping/HebrewShaper.cpp
)

# stubs/ comes first so the real GfxRenderer.h / BidiUtils.h are never seen.
target_include_directories(ParsedTextBidiTest BEFORE PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
)
target_include_directories(ParsedTextBidiTest PRIVATE
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Epub/Epub
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/lib/CjkBreak
  ${REPO_ROOT}/lib/CjkPunct
  ${REPO_ROOT}/lib/MiniBidi
  ${REPO_ROOT}/lib/TextShaping
  ${REPO_ROOT}/lib/Memory
)

target_compile_definitions(ParsedTextBidiTest PRIVATE
  HYPHENATION_RESOURCES_DIR="${REPO_ROOT}/test/hyphenation_eval/resources"
)

target_link_libraries(ParsedTextBidiTest PRIVATE inkmod_test_common wh_minibidi_c GTest::gtest_main)

gtest_discover_tests(ParsedTextBidiTest)
