include(FetchContent)
if(NOT TARGET ArduinoJson)
  FetchContent_Declare(
    ArduinoJson
    GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git
    GIT_TAG v7.4.3
  )
  FetchContent_MakeAvailable(ArduinoJson)
endif()

add_executable(PluginEventsTest
  PluginEventsTest.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/PluginEvents/PluginEvents.cpp
)
target_include_directories(PluginEventsTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/PluginEvents
  ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/PluginCatalog)
target_link_libraries(PluginEventsTest PRIVATE ArduinoJson GTest::gtest_main)
gtest_discover_tests(PluginEventsTest)
