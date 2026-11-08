find_package(OpenSSL REQUIRED)

include(FetchContent)
FetchContent_Declare(
  ArduinoJson
  GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git
  GIT_TAG v7.4.3
)
FetchContent_MakeAvailable(ArduinoJson)

set(CP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../freeink-sdk/libs/book/ContentProtection)

add_executable(lcp_service_test
  LcpServiceTest.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/Epub/LcpService.cpp
  ${CP_DIR}/src/LcpLicense.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../../freeink-sdk/libs/network/JsonSax/src/StreamingJsonParser.cpp
)

target_include_directories(lcp_service_test PRIVATE
  ${CP_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/Epub
  ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/JsonParser
  ${CMAKE_CURRENT_SOURCE_DIR}/../../freeink-sdk/libs/network/JsonSax/include
)

target_link_libraries(lcp_service_test PRIVATE GTest::gtest_main OpenSSL::Crypto ArduinoJson)

gtest_discover_tests(lcp_service_test)
