add_executable(ZipCdWindowTest ZipCdWindowTest.cpp)
target_include_directories(ZipCdWindowTest PRIVATE ${REPO_ROOT}/lib/ZipFile)
target_link_libraries(ZipCdWindowTest PRIVATE inkmod_test_common GTest::gtest_main)
gtest_discover_tests(ZipCdWindowTest)
