add_executable(css_font_family_test CssFontFamilyTest.cpp)

target_include_directories(css_font_family_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/Epub/Epub/css
)

target_link_libraries(css_font_family_test PRIVATE GTest::gtest_main)

gtest_discover_tests(css_font_family_test)
