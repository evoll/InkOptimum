add_executable(I18nBlobTest I18nBlobTest.cpp ${REPO_ROOT}/lib/I18n/I18nStrings.cpp)
target_include_directories(I18nBlobTest PRIVATE ${REPO_ROOT}/lib/I18n)
target_link_libraries(I18nBlobTest PRIVATE GTest::gtest_main)
gtest_discover_tests(I18nBlobTest)
