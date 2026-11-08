add_executable(VerticalTextTest
  VerticalOrientationTest.cpp
  TurnedFrameTest.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/Epub/Epub/VerticalOrientation.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/Epub/Epub/FlowContext.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/CjkPunct/CjkPunct.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/Utf8/Utf8.cpp
)
target_include_directories(VerticalTextTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../lib
  ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/Epub
  ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/Utf8
  ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/CjkPunct
)
target_link_libraries(VerticalTextTest PRIVATE GTest::gtest_main)
gtest_discover_tests(VerticalTextTest)
