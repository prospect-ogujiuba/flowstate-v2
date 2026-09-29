cmake_minimum_required(VERSION 3.22)

# Flowstate Spike: Phase 0 throwaway plugin that serves two briefs:
#   docs/spikes/webview-host.md  (WebView UI inside real hosts)
#   docs/spikes/midi-out.md      (MIDI out and transport-locked audition)
#
# Targets
#   FlowstateSpike         instrument (IS_SYNTH, MIDI in + out)  VST3 / AU / Standalone
#   FlowstateSpikeMidiFx   MIDI effect (IS_MIDI_EFFECT)           AU (macOS) + VST3
#   flowstate_spike_scheduler_test   pure C++ scheduler tests (no JUCE)
#
# Options
#   FLOWSTATE_JUCE_DIR                 use a local JUCE checkout instead of FetchContent
#   FLOWSTATE_SPIKE_BUILD_PLUGIN       build the plugin targets (OFF = tests only, no JUCE)
#   FLOWSTATE_SPIKE_HEADLESS_CHECK     Linux compile check: JUCE_WEB_BROWSER=0 + native stub editor
#   FLOWSTATE_SPIKE_EDITOR_WANTS_KEYBOARD_FOCUS   JUCE EDITOR_WANTS_KEYBOARD_FOCUS (typing vs space-bar knob)
#   FLOWSTATE_WEBVIEW2_PACKAGE_LOCATION  Windows: folder containing a Microsoft.Web.WebView2* NuGet dir

project(FlowstateSpike VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version" FORCE)
endif()

option(FLOWSTATE_SPIKE_BUILD_PLUGIN "Build the JUCE plugin targets" ON)
option(FLOWSTATE_SPIKE_BUILD_TESTS "Build the pure C++ scheduler tests" ON)
option(FLOWSTATE_SPIKE_EDITOR_WANTS_KEYBOARD_FOCUS "Set JUCE EDITOR_WANTS_KEYBOARD_FOCUS" OFF)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(_headless_default ON)
else()
    set(_headless_default OFF)
endif()
option(FLOWSTATE_SPIKE_HEADLESS_CHECK
       "Build with JUCE_WEB_BROWSER=0 and a native stub editor (Linux compile check)" ${_headless_default})
set(FLOWSTATE_JUCE_DIR "" CACHE PATH "Local JUCE checkout (empty = FetchContent JUCE 9.0.2)")
set(FLOWSTATE_WEBVIEW2_PACKAGE_LOCATION "" CACHE PATH
    "Windows: directory containing a *Microsoft.Web.WebView2* NuGet package dir (empty = download)")

if(MSVC)
    add_compile_options(/utf-8)
else()
    add_compile_options(-Wall -Wextra)
endif()

# ------------------------------------------------------------------------------------------------
# Pure C++ audition core (no JUCE). Shared by the plugin and the tests.
add_library(flowstate_spike_audition STATIC
    src/AuditionScheduler.cpp
    src/AuditionScheduler.h)
target_include_directories(flowstate_spike_audition PUBLIC src)
set_target_properties(flowstate_spike_audition PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(FLOWSTATE_SPIKE_BUILD_TESTS)
    enable_testing()
    add_executable(flowstate_spike_scheduler_test tests/scheduler_test.cpp)
    target_link_libraries(flowstate_spike_scheduler_test PRIVATE flowstate_spike_audition)
    add_test(NAME scheduler COMMAND flowstate_spike_scheduler_test)
endif()

if(NOT FLOWSTATE_SPIKE_BUILD_PLUGIN)
    return()
endif()

# ------------------------------------------------------------------------------------------------
# JUCE
if(FLOWSTATE_JUCE_DIR)
    add_subdirectory("${FLOWSTATE_JUCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE" EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 9.0.2
        GIT_SHALLOW TRUE
        GIT_PROGRESS TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

if(NOT FLOWSTATE_SPIKE_HEADLESS_CHECK AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR
        "The WebView build is only supported on macOS and Windows for this spike. "
        "On Linux configure with -DFLOWSTATE_SPIKE_HEADLESS_CHECK=ON (the default).")
endif()

# Windows: JUCE's WebView2 backend needs the WebView2 SDK headers + WebView2LoaderStatic.lib.
# JUCE's FindWebView2.cmake looks for a *Microsoft.Web.WebView2* dir under
# JUCE_WEBVIEW2_PACKAGE_LOCATION. We download the NuGet package (a zip) once per build tree.
if(WIN32 AND NOT FLOWSTATE_SPIKE_HEADLESS_CHECK)
    set(_wv2_version "1.0.3485.44") # the version JUCE 9.0.2's FindWebView2.cmake recommends
    if(FLOWSTATE_WEBVIEW2_PACKAGE_LOCATION)
        set(JUCE_WEBVIEW2_PACKAGE_LOCATION "${FLOWSTATE_WEBVIEW2_PACKAGE_LOCATION}")
    else()
        set(_wv2_root "${CMAKE_BINARY_DIR}/_deps/webview2")
        set(_wv2_dir  "${_wv2_root}/packages/Microsoft.Web.WebView2.${_wv2_version}")
        set(_wv2_pkg  "${_wv2_root}/Microsoft.Web.WebView2.${_wv2_version}.nupkg")
        if(NOT EXISTS "${_wv2_dir}/build/native/include/WebView2.h")
            message(STATUS "Downloading Microsoft.Web.WebView2 ${_wv2_version} from nuget.org")
            file(DOWNLOAD
                "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/${_wv2_version}"
                "${_wv2_pkg}" STATUS _wv2_status TLS_VERIFY ON)
            list(GET _wv2_status 0 _wv2_code)
            if(NOT _wv2_code EQUAL 0)
                message(FATAL_ERROR "WebView2 download failed: ${_wv2_status}. "
                                    "Set FLOWSTATE_WEBVIEW2_PACKAGE_LOCATION to a local NuGet package folder.")
            endif()
            file(ARCHIVE_EXTRACT INPUT "${_wv2_pkg}" DESTINATION "${_wv2_dir}")
        endif()
        # Only the extracted dir may live here: FindWebView2 globs *Microsoft.Web.WebView2*.
        set(JUCE_WEBVIEW2_PACKAGE_LOCATION "${_wv2_root}/packages")
    endif()
endif()

# ------------------------------------------------------------------------------------------------
# Bundled UI (served by the WebBrowserComponent resource provider) + sample clip.
# The JUCE JS interop library is served from the JUCE checkout as juce/index.js.
set(_juce_interop_js "${JUCE_MODULES_DIR}/juce_gui_extra/native/typescript/webview-interop/dist/index.js")
if(NOT EXISTS "${_juce_interop_js}")
    message(FATAL_ERROR "JUCE webview interop JS not found at ${_juce_interop_js}")
endif()
set(_juce_interop_copy "${CMAKE_BINARY_DIR}/ui-bundle/juce_interop.js")
configure_file("${_juce_interop_js}" "${_juce_interop_copy}" COPYONLY)

juce_add_binary_data(FlowstateSpikeUi
    NAMESPACE FlowstateSpikeUi
    HEADER_NAME FlowstateSpikeUi.h
    SOURCES
        ui/index.html
        ui/app.js
        ui/pianoroll.js
        ui/style.css
        ui/sample.notes.json
        "${_juce_interop_copy}")
set_target_properties(FlowstateSpikeUi PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ------------------------------------------------------------------------------------------------
# Plugin targets
if(APPLE)
    set(_instrument_formats VST3 AU Standalone)
    set(_midifx_formats AU VST3)
else()
    set(_instrument_formats VST3 Standalone)
    set(_midifx_formats VST3)
endif()

set(_spike_sources
    src/ClipFile.cpp
    src/ClipFile.h
    src/PluginProcessor.cpp
    src/PluginProcessor.h
    src/PreviewSynth.h)

if(FLOWSTATE_SPIKE_HEADLESS_CHECK)
    list(APPEND _spike_sources src/HeadlessEditor.cpp)
else()
    list(APPEND _spike_sources src/WebEditor.cpp src/FocusRelease.h)
    if(APPLE)
        list(APPEND _spike_sources src/FocusRelease_mac.mm)
    elseif(WIN32)
        list(APPEND _spike_sources src/FocusRelease_windows.cpp)
    endif()
endif()

function(flowstate_spike_configure target)
    target_sources(${target} PRIVATE ${_spike_sources})
    target_include_directories(${target} PRIVATE src)
    target_compile_definitions(${target} PUBLIC
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        FLOWSTATE_SPIKE_HEADLESS=$<IF:$<BOOL:${FLOWSTATE_SPIKE_HEADLESS_CHECK}>,1,0>)
    # On Linux JUCE itself defines JUCE_WEB_BROWSER / JUCE_USE_CURL from NEEDS_WEB_BROWSER / NEEDS_CURL
    # (both FALSE here), so only define them elsewhere.
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_compile_definitions(${target} PUBLIC JUCE_USE_CURL=0)
        if(FLOWSTATE_SPIKE_HEADLESS_CHECK)
            target_compile_definitions(${target} PUBLIC JUCE_WEB_BROWSER=0)
        endif()
    endif()
    if(WIN32 AND NOT FLOWSTATE_SPIKE_HEADLESS_CHECK)
        target_compile_definitions(${target} PUBLIC JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1)
    endif()
    target_link_libraries(${target}
        PRIVATE
            flowstate_spike_audition
            FlowstateSpikeUi
            juce::juce_audio_utils
            juce::juce_gui_extra
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags)
    # JUCE's own warning flags are not applied: the spike should build warning-clean with ours.
endfunction()

if(FLOWSTATE_SPIKE_EDITOR_WANTS_KEYBOARD_FOCUS)
    set(_wants_kb TRUE)
else()
    set(_wants_kb FALSE)
endif()

if(FLOWSTATE_SPIKE_HEADLESS_CHECK OR NOT WIN32)
    set(_needs_webview2 FALSE)
else()
    set(_needs_webview2 TRUE)
endif()

juce_add_plugin(FlowstateSpike
    PRODUCT_NAME "Flowstate Spike"
    COMPANY_NAME "Flowstate"
    COMPANY_WEBSITE "https://flowstate.invalid"
    BUNDLE_ID "com.flowstate.spike.instrument"
    PLUGIN_MANUFACTURER_CODE Flws
    PLUGIN_CODE Fsp1
    FORMATS ${_instrument_formats}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS ${_wants_kb}
    NEEDS_WEBVIEW2 ${_needs_webview2}
    VST3_CATEGORIES Instrument Synth
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    COPY_PLUGIN_AFTER_BUILD FALSE)
flowstate_spike_configure(FlowstateSpike)

juce_add_plugin(FlowstateSpikeMidiFx
    PRODUCT_NAME "Flowstate Spike MIDI FX"
    COMPANY_NAME "Flowstate"
    COMPANY_WEBSITE "https://flowstate.invalid"
    BUNDLE_ID "com.flowstate.spike.midifx"
    PLUGIN_MANUFACTURER_CODE Flws
    PLUGIN_CODE Fspm
    FORMATS ${_midifx_formats}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT TRUE
    EDITOR_WANTS_KEYBOARD_FOCUS ${_wants_kb}
    NEEDS_WEBVIEW2 ${_needs_webview2}
    VST3_CATEGORIES Fx Tools
    AU_MAIN_TYPE kAudioUnitType_MIDIProcessor
    COPY_PLUGIN_AFTER_BUILD FALSE)
flowstate_spike_configure(FlowstateSpikeMidiFx)

# ------------------------------------------------------------------------------------------------
# Host smoke test: loads the built VST3s through JUCE's VST3 host and drives a fake transport.
# Default ON for the headless (Linux) check, where it is the only end-to-end test we can run.
option(FLOWSTATE_SPIKE_HOST_SMOKE "Build and register the VST3 host smoke test" ${FLOWSTATE_SPIKE_HEADLESS_CHECK})

if(FLOWSTATE_SPIKE_BUILD_TESTS AND FLOWSTATE_SPIKE_HOST_SMOKE)
    juce_add_console_app(flowstate_spike_host_smoke PRODUCT_NAME "Flowstate Spike Host Smoke")
    target_sources(flowstate_spike_host_smoke PRIVATE tests/host_smoke.cpp)
    target_compile_definitions(flowstate_spike_host_smoke PRIVATE
        JUCE_PLUGINHOST_VST3=1
        JUCE_USE_CURL=0
        JUCE_WEB_BROWSER=0
        JUCE_STANDALONE_APPLICATION=1
        JUCE_DISPLAY_SPLASH_SCREEN=0)
    target_link_libraries(flowstate_spike_host_smoke PRIVATE
        juce::juce_audio_processors
        juce::juce_recommended_config_flags)
    add_dependencies(flowstate_spike_host_smoke FlowstateSpike_VST3 FlowstateSpikeMidiFx_VST3)
    add_test(NAME host_smoke
             COMMAND flowstate_spike_host_smoke
                     "$<GENEX_EVAL:$<TARGET_PROPERTY:FlowstateSpike_VST3,JUCE_PLUGIN_ARTEFACT_FILE>>"
                     "$<GENEX_EVAL:$<TARGET_PROPERTY:FlowstateSpikeMidiFx_VST3,JUCE_PLUGIN_ARTEFACT_FILE>>")
endif()
