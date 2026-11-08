add_executable(download_sniff_test DownloadSniffTest.cpp)
target_include_directories(download_sniff_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/OpdsParser)
target_link_libraries(download_sniff_test PRIVATE GTest::gtest_main)
gtest_discover_tests(download_sniff_test)
