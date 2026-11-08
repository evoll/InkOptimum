find_package(OpenSSL REQUIRED)

set(CP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../freeink-sdk/libs/book/ContentProtection)

add_executable(lcp_unlock_test
  LcpUnlockTest.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/Epub/LcpUnlock.cpp
  ${CP_DIR}/src/LcpLicense.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../../freeink-sdk/libs/network/JsonSax/src/StreamingJsonParser.cpp
)

target_include_directories(lcp_unlock_test PRIVATE
  ${CP_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/Epub
  ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/JsonParser
  ${CMAKE_CURRENT_SOURCE_DIR}/../../freeink-sdk/libs/network/JsonSax/include
)

target_link_libraries(lcp_unlock_test PRIVATE GTest::gtest_main OpenSSL::Crypto)

gtest_discover_tests(lcp_unlock_test)
