add_executable(cjk_punct_test CjkPunctTest.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/CjkPunct/CjkPunct.cpp)
target_include_directories(cjk_punct_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/CjkPunct)
target_link_libraries(cjk_punct_test PRIVATE GTest::gtest_main)
gtest_discover_tests(cjk_punct_test)
