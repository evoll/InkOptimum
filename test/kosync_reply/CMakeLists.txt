include(FetchContent)

FetchContent_Declare(
  ArduinoJson
  GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git
  GIT_TAG v7.4.3
)
FetchContent_MakeAvailable(ArduinoJson)

add_executable(kosync_reply_test KOReaderProgressReplyTest.cpp)

target_include_directories(kosync_reply_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/KOReaderSync
)

target_link_libraries(kosync_reply_test PRIVATE
  ArduinoJson
  GTest::gtest_main
)

gtest_discover_tests(kosync_reply_test)
