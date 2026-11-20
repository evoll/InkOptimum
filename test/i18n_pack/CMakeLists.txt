add_executable(I18nPackTest I18nPackTest.cpp)
target_include_directories(I18nPackTest PRIVATE ${REPO_ROOT}/lib/I18n)
target_link_libraries(I18nPackTest PRIVATE GTest::gtest_main)
gtest_discover_tests(I18nPackTest)
