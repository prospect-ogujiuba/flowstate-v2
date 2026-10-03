cmake_minimum_required(VERSION 3.22)
project(flowstate_core VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(FLOWSTATE_CORE_BUILD_TESTS "Build flowstate_core tests" ON)
option(FLOWSTATE_CORE_BUILD_CLI "Build the fs-realize CLI" ON)

include(FetchContent)
FetchContent_Declare(nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(nlohmann_json)

# Warnings apply to our own targets only.
function(flowstate_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive-)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wshadow)
  endif()
endfunction()

add_library(flowstate_core STATIC
  src/ir.cpp
  src/theory.cpp
  src/chord_parser.cpp
  src/timing.cpp
  src/grooves.cpp
  src/steps.cpp
  src/groove.cpp
  src/harmony.cpp
  src/voicing.cpp
  src/constraints.cpp
  src/realize.cpp
  src/realize_chords.cpp
  src/realize_arp.cpp
  src/realize_bass.cpp
  src/realize_melody.cpp
  src/realize_drums.cpp
  src/smf.cpp
  src/output.cpp
  src/midi_read.cpp
  src/analyze.cpp
  src/catalog.cpp
  src/sketch.cpp)
target_include_directories(flowstate_core PUBLIC include PRIVATE src)
target_link_libraries(flowstate_core PRIVATE nlohmann_json::nlohmann_json)
flowstate_warnings(flowstate_core)
if(NOT MSVC)
  target_compile_options(flowstate_core PRIVATE -Wpedantic)
endif()

if(FLOWSTATE_CORE_BUILD_CLI)
  add_executable(fs-realize cli/fs_realize.cpp)
  target_link_libraries(fs-realize PRIVATE flowstate_core)
  flowstate_warnings(fs-realize)
  add_executable(fs-analyze cli/fs_analyze.cpp)
  target_link_libraries(fs-analyze PRIVATE flowstate_core)
  flowstate_warnings(fs-analyze)
endif()

if(FLOWSTATE_CORE_BUILD_TESTS)
  enable_testing()
  # Header-only: download, but do not add doctest's own CMake project.
  FetchContent_Declare(doctest
    URL https://github.com/doctest/doctest/archive/refs/tags/v2.5.3.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR do-not-add)
  FetchContent_MakeAvailable(doctest)
  add_library(flowstate_doctest INTERFACE)
  target_include_directories(flowstate_doctest SYSTEM INTERFACE ${doctest_SOURCE_DIR})

  add_executable(flowstate_core_tests
    tests/main.cpp
    tests/test_theory.cpp
    tests/test_steps_timing.cpp
    tests/test_voicing.cpp
    tests/test_realize.cpp
    tests/test_smf.cpp
    tests/test_grooves.cpp
    tests/test_analyze.cpp
    tests/test_catalog.cpp
    tests/test_sketch.cpp)
  target_link_libraries(flowstate_core_tests PRIVATE flowstate_core flowstate_doctest nlohmann_json::nlohmann_json)
  target_include_directories(flowstate_core_tests PRIVATE src)
  target_compile_definitions(flowstate_core_tests PRIVATE
    FLOWSTATE_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures"
    FLOWSTATE_SCORE_SCHEMA="${CMAKE_CURRENT_SOURCE_DIR}/../schema/score.v0.schema.json")
  flowstate_warnings(flowstate_core_tests)
  add_test(NAME flowstate_core_tests COMMAND flowstate_core_tests)
endif()
