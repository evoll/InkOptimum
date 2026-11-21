add_executable(reading_daily_test ReadingDailyTest.cpp ${REPO_ROOT}/lib/ReadingDaily/DailyReadingSeconds.cpp)
target_include_directories(reading_daily_test PRIVATE ${REPO_ROOT}/lib/ReadingDaily)
target_link_libraries(reading_daily_test PRIVATE GTest::gtest_main)
gtest_discover_tests(reading_daily_test)
