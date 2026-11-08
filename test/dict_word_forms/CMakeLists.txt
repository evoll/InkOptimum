add_executable(dict_word_forms_test DictWordFormsTest.cpp)

target_include_directories(dict_word_forms_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/DictWordForms
)

target_link_libraries(dict_word_forms_test PRIVATE GTest::gtest_main)

gtest_discover_tests(dict_word_forms_test)
