enable_language(C)

file(GLOB XTAPP_LUA_SOURCES ${REPO_ROOT}/lib/Lua/*.c)
add_library(xtapp_lua STATIC ${XTAPP_LUA_SOURCES})
target_include_directories(xtapp_lua PUBLIC ${REPO_ROOT}/lib/Lua)
target_compile_definitions(xtapp_lua PUBLIC LUA_USE_C89)
target_compile_options(xtapp_lua PRIVATE -w)

add_executable(xtapp_test
  XtAppTest.cpp
  ${REPO_ROOT}/lib/XtApp/XtApp.cpp
  ${REPO_ROOT}/lib/XtApp/XtAppJson.cpp
  ${REPO_ROOT}/lib/XtApp/XtAppUtil.cpp)
target_include_directories(xtapp_test PRIVATE ${REPO_ROOT}/lib/XtApp)
target_link_libraries(xtapp_test PRIVATE xtapp_lua inkmod_test_common GTest::gtest_main)
gtest_discover_tests(xtapp_test)
