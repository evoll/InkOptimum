add_executable(PluginJobsTest
  PluginJobsTest.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/PluginJobs/PluginJobs.cpp
)
target_include_directories(PluginJobsTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/PluginJobs)
target_link_libraries(PluginJobsTest PRIVATE GTest::gtest_main)
gtest_discover_tests(PluginJobsTest)
