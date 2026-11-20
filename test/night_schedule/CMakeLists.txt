add_executable(night_schedule_test NightScheduleTest.cpp)

target_include_directories(night_schedule_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src/util
)

target_link_libraries(night_schedule_test PRIVATE GTest::gtest_main)

gtest_discover_tests(night_schedule_test)
