cmake_minimum_required(VERSION 3.22)

# Flowstate plugin (P1-6): the product shell.
#
# Targets
#   flowstate_session         JUCE-free session, controller, capture, audition and state (links core + bridge)
#   flowstate_session_tests   doctest suite for the above
#   flowstate_scheduler_tests the audition scheduler: the spike's sync checks plus bar-quantized switching
#   Flowstate                 instrument (IS_SYNTH, MIDI in + out)   VST3 / AU / Standalone
#   FlowstateMidiFx           MIDI effect (IS_MIDI_EFFECT)            AU (macOS) + VST3
#   flowstate_plugin_tests    processor-level tests (JUCE console app linking the instrument's shared code)
#   flowstate_host_smoke      loads the built VST3s through JUCE's VST3 host
#
# Options
#   FLOWSTATE_PLUGIN_BUILD_PLUGIN     build the JUCE targets (OFF = session library and tests only)
#   FLOWSTATE_PLUGIN_BUILD_TESTS      build the tests
#   FLOWSTATE_PLUGIN_HEADLESS         JUCE_WEB_BROWSER=0 and a native stub editor (default on Linux)
#   FLOWSTATE_JUCE_DIR                local JUCE checkout instead of FetchContent JUCE 9.0.2
#   FLOWSTATE_WEBVIEW2_PACKAGE_LOCATION  Windows: folder containing a Microsoft.Web.WebView2* NuGet dir
#   FLOWSTATE_BUILD_ID                shown in Settings (CI sets it; P1-15)
#   FLOWSTATE_PLUGIN_LTO              JUCE's recommended LTO flags (ON; turn OFF for faster local builds)

project(Flowstate VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version" FORCE)
endif()

option(FLOWSTATE_PLUGIN_BUILD_PLUGIN "Build the JUCE plugin targets" ON)
option(FLOWSTATE_PLUGIN_BUILD_TESTS "Build the tests" ON)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(_headless_default ON)
else()
    set(_headless_default OFF)
endif()
option(FLOWSTATE_PLUGIN_HEADLESS "JUCE_WEB_BROWSER=0 and a native stub editor" ${_headless_default})
set(FLOWSTATE_JUCE_DIR "" CACHE PATH "Local JUCE checkout (empty = FetchContent JUCE 9.0.2)")
set(FLOWSTATE_WEBVIEW2_PACKAGE_LOCATION "" CACHE PATH
    "Windows: directory containing a *Microsoft.Web.WebView2* NuGet package dir (empty = download)")
set(FLOWSTATE_BUILD_ID "dev" CACHE STRING "Build id shown in Settings")
option(FLOWSTATE_PLUGIN_LTO "Link-time optimization for the plugin targets (off speeds up local builds)" ON)

function(flowstate_plugin_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /bigobj)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wshadow)
    endif()
endfunction()

# ------------------------------------------------------------------------------------------------
# core and the generated bridge types (core first: it declares nlohmann_json, which the bridge reuses)
set(FLOWSTATE_CORE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FLOWSTATE_CORE_BUILD_CLI OFF CACHE BOOL "" FORCE)
add_subdirectory(../core "${CMAKE_BINARY_DIR}/core")
set(FLOWSTATE_BRIDGE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(../schema/cpp "${CMAKE_BINARY_DIR}/bridge")
set_target_properties(flowstate_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(flowstate_session STATIC
    src/session/Audition.cpp
    src/session/Capture.cpp
    src/session/Controller.cpp
    src/session/Library.cpp
    src/session/PluginState.cpp
    src/session/Session.cpp
    src/session/Sse.cpp)
target_include_directories(flowstate_session PUBLIC src)
target_link_libraries(flowstate_session PUBLIC flowstate_core flowstate::bridge)
set_target_properties(flowstate_session PROPERTIES POSITION_INDEPENDENT_CODE ON)
flowstate_plugin_warnings(flowstate_session)

if(FLOWSTATE_PLUGIN_BUILD_TESTS)
    enable_testing()
    include(FetchContent)
    FetchContent_Declare(doctest
        URL https://github.com/doctest/doctest/archive/refs/tags/v2.5.3.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SOURCE_SUBDIR do-not-add)
    FetchContent_MakeAvailable(doctest)

    add_executable(flowstate_session_tests tests/session_test.cpp)
    target_link_libraries(flowstate_session_tests PRIVATE flowstate_session)
    target_include_directories(flowstate_session_tests SYSTEM PRIVATE ${doctest_SOURCE_DIR})
    target_compile_definitions(flowstate_session_tests PRIVATE
        FLOWSTATE_CORE_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../core/tests/fixtures"
        FLOWSTATE_LIBRARY_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../library/catalog")
    flowstate_plugin_warnings(flowstate_session_tests)
    add_test(NAME session COMMAND flowstate_session_tests)

    add_executable(flowstate_scheduler_tests tests/scheduler_test.cpp)
    target_link_libraries(flowstate_scheduler_tests PRIVATE flowstate_session)
    flowstate_plugin_warnings(flowstate_scheduler_tests)
    add_test(NAME scheduler COMMAND flowstate_scheduler_tests)
endif()

if(NOT FLOWSTATE_PLUGIN_BUILD_PLUGIN)
    return()
endif()

include(cmake/juce-targets.cmake)
