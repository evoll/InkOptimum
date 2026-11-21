include(FetchContent)

FetchContent_Declare(
  ArduinoJson
  GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git
  GIT_TAG v7.4.3
)
FetchContent_MakeAvailable(ArduinoJson)

add_executable(reading_sessions_test ReadingSessionsTest.cpp ${REPO_ROOT}/lib/ReadingSessions/ReadingSessions.cpp)
target_include_directories(reading_sessions_test PRIVATE ${REPO_ROOT}/lib/ReadingSessions)
target_link_libraries(reading_sessions_test PRIVATE ArduinoJson GTest::gtest_main)
gtest_discover_tests(reading_sessions_test)
