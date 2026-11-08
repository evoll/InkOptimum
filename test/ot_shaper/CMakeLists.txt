file(GLOB OT_SHAPER_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/OtShaper/*.cpp)
add_executable(ot_shaper_test OtShaperTest.cpp ${OT_SHAPER_SOURCES} ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/Utf8/Utf8.cpp)
target_include_directories(ot_shaper_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/OtShaper
  ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/Utf8
  ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/Memory)
target_compile_definitions(ot_shaper_test PRIVATE OT_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
target_link_libraries(ot_shaper_test PRIVATE GTest::gtest_main)
gtest_discover_tests(ot_shaper_test)
