include(FetchContent)
if(NOT TARGET ArduinoJson)
  FetchContent_Declare(
    ArduinoJson
    GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git
    GIT_TAG v7.4.3
  )
  FetchContent_MakeAvailable(ArduinoJson)
endif()

add_executable(PluginCatalogTest
  PluginCatalogTest.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/PluginCatalog/PluginManifest.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/PluginCatalog/PluginItems.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/PluginCatalog/PluginXml.cpp
)
target_include_directories(PluginCatalogTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/PluginCatalog)
find_library(SYSTEM_EXPAT expat)
if(NOT SYSTEM_EXPAT)
  message(FATAL_ERROR "libexpat-dev is needed for the plugin XML catalog tests")
endif()
target_link_libraries(PluginCatalogTest PRIVATE ArduinoJson ${SYSTEM_EXPAT} GTest::gtest_main)
gtest_discover_tests(PluginCatalogTest)
