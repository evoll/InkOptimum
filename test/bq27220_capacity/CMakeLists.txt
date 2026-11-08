add_executable(bq27220_capacity_test Bq27220CapacityTest.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/hal/Bq27220Capacity.cpp)

target_include_directories(bq27220_capacity_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/hal
)

target_link_libraries(bq27220_capacity_test PRIVATE GTest::gtest_main)

gtest_discover_tests(bq27220_capacity_test)
