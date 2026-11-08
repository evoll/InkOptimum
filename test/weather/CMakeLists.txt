add_executable(WeatherTest
  WeatherTest.cpp
  ${REPO_ROOT}/lib/Weather/WeatherParser.cpp
  ${REPO_ROOT}/lib/Weather/WeatherSettings.cpp
  ${REPO_ROOT}/freeink-sdk/libs/network/JsonSax/src/StreamingJsonParser.cpp
)
target_include_directories(WeatherTest PRIVATE ${REPO_ROOT}/lib/Weather ${REPO_ROOT}/lib/JsonParser
  ${REPO_ROOT}/freeink-sdk/libs/network/JsonSax/include)
target_link_libraries(WeatherTest PRIVATE inkmod_test_common GTest::gtest_main)
gtest_discover_tests(WeatherTest)
