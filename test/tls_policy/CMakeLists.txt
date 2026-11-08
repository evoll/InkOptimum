include(FetchContent)
if(NOT TARGET ArduinoJson)
  FetchContent_Declare(
    ArduinoJson
    GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git
    GIT_TAG v7.4.3
  )
  FetchContent_MakeAvailable(ArduinoJson)
endif()

add_executable(TlsPolicyTest TlsPolicyTest.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/TlsPolicy/TlsPolicy.cpp)
target_include_directories(TlsPolicyTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/TlsPolicy)
target_link_libraries(TlsPolicyTest PRIVATE ArduinoJson GTest::gtest_main)
gtest_discover_tests(TlsPolicyTest)
