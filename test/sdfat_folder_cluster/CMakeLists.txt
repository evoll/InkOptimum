find_package(Python3 REQUIRED COMPONENTS Interpreter)

# SdFat 2.3.1 as the firmware builds it: scripts/sdfat_patches/ applied by scripts/patch_sdfat.py (the same digest
# gates as the firmware build), over an in-memory card. The test pins what patches 0005/0006 promise: a new
# folder's cluster comes out zero whatever it held, in far fewer card write commands.
# SdFat is fetched at configure time like googletest; with -DINKMOD_SDFAT_DIR=<an unpatched SdFat 2.3.1 checkout>
# (library.properties + src/) no download is needed (offline machines).
if(INKMOD_SDFAT_DIR)
  set(sdfat_SOURCE_DIR "${INKMOD_SDFAT_DIR}")
else()
  include(FetchContent)
  FetchContent_Declare(
    sdfat
    URL https://github.com/greiman/SdFat/archive/refs/tags/2.3.1.tar.gz
    URL_HASH SHA256=a86ac8c6551f8294689569cf748f6aa81ceddd2317ef4f0815a0bb39b365c769
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR no-cmake  # SdFat has no CMakeLists.txt; populate only
  )
  FetchContent_MakeAvailable(sdfat)
endif()

# Patch a fresh copy, so an edited patch never meets a copy patched by the last one.
set(SDFAT_PATCHED_DIR "${CMAKE_CURRENT_BINARY_DIR}/SdFat")
file(REMOVE_RECURSE "${SDFAT_PATCHED_DIR}")
file(COPY "${sdfat_SOURCE_DIR}/library.properties" "${sdfat_SOURCE_DIR}/src" DESTINATION "${SDFAT_PATCHED_DIR}")
file(GLOB SDFAT_PATCH_FILES "${REPO_ROOT}/scripts/sdfat_patches/*.patch")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${REPO_ROOT}/scripts/patch_sdfat.py" ${SDFAT_PATCH_FILES})
execute_process(
  COMMAND ${Python3_EXECUTABLE} "${REPO_ROOT}/scripts/patch_sdfat.py" "${SDFAT_PATCHED_DIR}"
  RESULT_VARIABLE SDFAT_PATCH_RESULT
  ERROR_VARIABLE SDFAT_PATCH_ERROR
)
if(NOT SDFAT_PATCH_RESULT EQUAL 0 OR NOT "${SDFAT_PATCH_ERROR}" STREQUAL "")
  message(FATAL_ERROR "scripts/patch_sdfat.py did not patch ${SDFAT_PATCHED_DIR}: ${SDFAT_PATCH_ERROR}")
endif()

file(GLOB SDFAT_HOST_SOURCES
  "${SDFAT_PATCHED_DIR}/src/FatLib/*.cpp"
  "${SDFAT_PATCHED_DIR}/src/ExFatLib/*.cpp"
  "${SDFAT_PATCHED_DIR}/src/FsLib/*.cpp"
  "${SDFAT_PATCHED_DIR}/src/common/*.cpp"
)
add_library(sdfat_host STATIC ${SDFAT_HOST_SOURCES})
# host/ first: it stands in for SdCard/SdCard.h, which only the SPI card driver needs.
target_include_directories(sdfat_host PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/host" "${SDFAT_PATCHED_DIR}/src")
# The SdFat options platformio.ini sets, plus the block-device API the X4 Pro mounts through.
target_compile_definitions(sdfat_host PUBLIC
  ENABLE_ARDUINO_FEATURES=0
  ENABLE_ARDUINO_SERIAL=0
  ENABLE_ARDUINO_STRING=0
  USE_BLOCK_DEVICE_INTERFACE=1
  MAINTAIN_FREE_CLUSTER_COUNT=1
  USE_UTF8_LONG_NAMES=1
  DESTRUCTOR_CLOSES_FILE=1
)
target_compile_options(sdfat_host PUBLIC -include "${CMAKE_CURRENT_SOURCE_DIR}/host/host_prelude.h")
target_compile_options(sdfat_host PRIVATE -w)  # third-party code, as it ships

add_executable(SdFatFolderClusterTest SdFatFolderClusterTest.cpp)
target_link_libraries(SdFatFolderClusterTest PRIVATE sdfat_host GTest::gtest_main)
gtest_discover_tests(SdFatFolderClusterTest)
