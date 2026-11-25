add_executable(pin_lock_test
  PinLockTest.cpp
  ${REPO_ROOT}/lib/PinLock/PinLock.cpp
  ${REPO_ROOT}/lib/CompanionAuth/CompanionAuth.cpp
  ${REPO_ROOT}/lib/XtApp/XtAppUtil.cpp)
target_link_libraries(pin_lock_test PRIVATE inkmod_test_common GTest::gtest_main)
gtest_discover_tests(pin_lock_test)
target_include_directories(pin_lock_test PRIVATE ${REPO_ROOT}/lib/XtApp ${REPO_ROOT}/lib/CompanionAuth ${REPO_ROOT}/lib/PinLock)
