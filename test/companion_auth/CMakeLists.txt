add_executable(companion_auth_test
  CompanionAuthTest.cpp
  ${REPO_ROOT}/lib/CompanionAuth/CompanionAuth.cpp
  ${REPO_ROOT}/lib/XtApp/XtAppUtil.cpp)
target_link_libraries(companion_auth_test PRIVATE inkmod_test_common GTest::gtest_main)
gtest_discover_tests(companion_auth_test)
target_include_directories(companion_auth_test PRIVATE ${REPO_ROOT}/lib/XtApp ${REPO_ROOT}/lib/CompanionAuth)
