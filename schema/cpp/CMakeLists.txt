# flowstate_bridge: the generated bridge types (include/flowstate/bridge.h), header-only.
# The plugin adds this directory and links flowstate::bridge.
cmake_minimum_required(VERSION 3.22)
project(flowstate_bridge LANGUAGES CXX)

option(FLOWSTATE_BRIDGE_BUILD_TESTS "Build the bridge round-trip tests" ${PROJECT_IS_TOP_LEVEL})

include(FetchContent)
if(NOT TARGET nlohmann_json::nlohmann_json)
  FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(nlohmann_json)
endif()

add_library(flowstate_bridge INTERFACE)
add_library(flowstate::bridge ALIAS flowstate_bridge)
target_include_directories(flowstate_bridge INTERFACE include)
target_link_libraries(flowstate_bridge INTERFACE nlohmann_json::nlohmann_json)
target_compile_features(flowstate_bridge INTERFACE cxx_std_20)

if(FLOWSTATE_BRIDGE_BUILD_TESTS)
  enable_testing()
  FetchContent_Declare(doctest
    URL https://github.com/doctest/doctest/archive/refs/tags/v2.5.3.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR do-not-add)
  FetchContent_MakeAvailable(doctest)

  add_executable(flowstate_bridge_tests tests/bridge_test.cpp)
  target_link_libraries(flowstate_bridge_tests PRIVATE flowstate::bridge)
  target_include_directories(flowstate_bridge_tests SYSTEM PRIVATE ${doctest_SOURCE_DIR})
  target_compile_definitions(flowstate_bridge_tests PRIVATE
    FLOWSTATE_BRIDGE_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../fixtures/bridge")
  if(MSVC)
    target_compile_options(flowstate_bridge_tests PRIVATE /W4 /permissive- /bigobj)
  else()
    target_compile_options(flowstate_bridge_tests PRIVATE -Wall -Wextra -Wshadow -Wpedantic)
  endif()
  add_test(NAME flowstate_bridge_tests COMMAND flowstate_bridge_tests)
endif()
