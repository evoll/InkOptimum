add_executable(font_upload_rules_test FontUploadRulesTest.cpp)

target_include_directories(font_upload_rules_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/EpdFont
)

target_link_libraries(font_upload_rules_test PRIVATE GTest::gtest_main)

gtest_discover_tests(font_upload_rules_test)
