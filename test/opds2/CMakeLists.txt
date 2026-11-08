add_executable(Opds2FeedTest Opds2FeedTest.cpp
  ${REPO_ROOT}/lib/Opds2/Opds2Feed.cpp
  ${REPO_ROOT}/freeink-sdk/libs/network/JsonSax/src/StreamingJsonParser.cpp)
target_include_directories(Opds2FeedTest PRIVATE
  ${REPO_ROOT}/lib/Opds2
  ${REPO_ROOT}/lib/OpdsParser
  ${REPO_ROOT}/lib/OpdsMeta
  ${REPO_ROOT}/lib/JsonParser
  ${REPO_ROOT}/freeink-sdk/libs/network/JsonSax/include)
target_link_libraries(Opds2FeedTest PRIVATE inkmod_test_common GTest::gtest_main)
gtest_discover_tests(Opds2FeedTest)
