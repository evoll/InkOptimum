add_executable(book_text_search_test BookTextSearchTest.cpp)

target_include_directories(book_text_search_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/Epub
  ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/DictWordForms
)

target_link_libraries(book_text_search_test PRIVATE GTest::gtest_main)

gtest_discover_tests(book_text_search_test)
