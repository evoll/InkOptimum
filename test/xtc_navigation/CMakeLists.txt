add_executable(xtc_navigation_test XtcNavigationTest.cpp)

target_include_directories(xtc_navigation_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src/activities/reader
)

target_link_libraries(xtc_navigation_test PRIVATE GTest::gtest_main)

gtest_discover_tests(xtc_navigation_test)
