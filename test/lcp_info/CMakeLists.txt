include(FetchContent)
FetchContent_Declare(
  ArduinoJson
  GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git
  GIT_TAG v7.4.3
)
FetchContent_MakeAvailable(ArduinoJson)

add_executable(lcp_info_test
  LcpInfoTest.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/Epub/LcpInfo.cpp
)
target_include_directories(lcp_info_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/Epub)
target_link_libraries(lcp_info_test PRIVATE GTest::gtest_main ArduinoJson)
gtest_discover_tests(lcp_info_test)
