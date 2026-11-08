project(mini_bidi_test LANGUAGES C CXX)

add_library(minibidi_c STATIC ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/MiniBidi/minibidi.c)
target_include_directories(minibidi_c PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/MiniBidi)
add_executable(mini_bidi_test MiniBidiTest.cpp)
target_link_libraries(mini_bidi_test PRIVATE minibidi_c GTest::gtest_main)
gtest_discover_tests(mini_bidi_test)
