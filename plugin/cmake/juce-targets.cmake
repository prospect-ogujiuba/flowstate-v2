# JUCE targets for the Flowstate plugin. Included from plugin/CMakeLists.txt.

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

if(NOT FLOWSTATE_PLUGIN_HEADLESS AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "The WebView build supports macOS and Windows; on Linux use -DFLOWSTATE_PLUGIN_HEADLESS=ON (the default).")
endif()

# Windows: JUCE's WebView2 backend needs the WebView2 SDK (headers + WebView2LoaderStatic.lib).
# JUCE's FindWebView2.cmake looks for a *Microsoft.Web.WebView2* dir under JUCE_WEBVIEW2_PACKAGE_LOCATION.
if(WIN32 AND NOT FLOWSTATE_PLUGIN_HEADLESS)
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
        set(JUCE_WEBVIEW2_PACKAGE_LOCATION "${_wv2_root}/packages")
    endif()
endif()

# ------------------------------------------------------------------------------------------------
# Bundled UI: the ui/ build output (`npm run build:ui`, flat: the resource provider looks files up
# by name), plus JUCE's interop library as juce_interop.js.
set(_juce_interop_js "${JUCE_MODULES_DIR}/juce_gui_extra/native/typescript/webview-interop/dist/index.js")
if(NOT EXISTS "${_juce_interop_js}")
    message(FATAL_ERROR "JUCE webview interop JS not found at ${_juce_interop_js}")
endif()
set(_juce_interop_copy "${CMAKE_BINARY_DIR}/ui-bundle/juce_interop.js")
configure_file("${_juce_interop_js}" "${_juce_interop_copy}" COPYONLY)

set(FLOWSTATE_UI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../ui/dist" CACHE PATH "Built UI to bundle (npm run build:ui)")
if(NOT EXISTS "${FLOWSTATE_UI_DIR}/index.html")
    if(FLOWSTATE_PLUGIN_HEADLESS)
        message(WARNING "No built UI in ${FLOWSTATE_UI_DIR}; the headless build doesn't show it. Run `npm run build:ui` to bundle it.")
    else()
        message(FATAL_ERROR "No built UI in ${FLOWSTATE_UI_DIR}. Run `npm ci && npm run build:ui` first.")
    endif()
endif()
file(GLOB _ui_files CONFIGURE_DEPENDS
    "${FLOWSTATE_UI_DIR}/*.html" "${FLOWSTATE_UI_DIR}/*.js" "${FLOWSTATE_UI_DIR}/*.css"
    "${FLOWSTATE_UI_DIR}/*.png" "${FLOWSTATE_UI_DIR}/*.svg" "${FLOWSTATE_UI_DIR}/*.woff2")
juce_add_binary_data(FlowstateUi
    NAMESPACE FlowstateUi
    HEADER_NAME FlowstateUi.h
    SOURCES ${_ui_files} "${_juce_interop_copy}")
set_target_properties(FlowstateUi PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Bundled MIDI library: library/catalog (catalog.json and the normalized clips, flat), generated and
# committed by `npm run -w library build` (docs/library.md).
file(GLOB _library_files CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/../library/catalog/*.json" "${CMAKE_CURRENT_SOURCE_DIR}/../library/catalog/*.mid")
if(NOT _library_files)
    message(FATAL_ERROR "No library catalog in library/catalog. Run `npm run -w library build`.")
endif()
juce_add_binary_data(FlowstateLibrary
    NAMESPACE FlowstateLibrary
    HEADER_NAME FlowstateLibrary.h
    SOURCES ${_library_files})
set_target_properties(FlowstateLibrary PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ------------------------------------------------------------------------------------------------
# Plugins
if(APPLE)
    set(_instrument_formats VST3 AU Standalone)
    set(_midifx_formats AU VST3)
else()
    set(_instrument_formats VST3 Standalone)
    set(_midifx_formats VST3)
endif()

configure_file(src/BuildId.cpp.in "${CMAKE_CURRENT_BINARY_DIR}/generated/BuildId.cpp" @ONLY)
set(_build_id_source "${CMAKE_CURRENT_BINARY_DIR}/generated/BuildId.cpp")
set(_plugin_sources src/MidiFiles.cpp src/MidiFiles.h src/PluginProcessor.cpp src/PluginProcessor.h
    src/BuildId.h "${_build_id_source}")
if(FLOWSTATE_PLUGIN_HEADLESS)
    list(APPEND _plugin_sources src/HeadlessEditor.cpp)
else()
    list(APPEND _plugin_sources src/WebEditor.cpp src/FocusRelease.h)
    if(APPLE)
        list(APPEND _plugin_sources src/FocusRelease_mac.mm)
    elseif(WIN32)
        list(APPEND _plugin_sources src/FocusRelease_windows.cpp)
    endif()
endif()

function(flowstate_configure_plugin target)
    target_sources(${target} PRIVATE ${_plugin_sources})
    target_include_directories(${target} PRIVATE src)
    target_compile_definitions(${target} PUBLIC
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)
    # On Linux JUCE defines JUCE_WEB_BROWSER / JUCE_USE_CURL from NEEDS_WEB_BROWSER / NEEDS_CURL.
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_compile_definitions(${target} PUBLIC JUCE_USE_CURL=0)
        if(FLOWSTATE_PLUGIN_HEADLESS)
            target_compile_definitions(${target} PUBLIC JUCE_WEB_BROWSER=0)
        endif()
    endif()
    if(WIN32 AND NOT FLOWSTATE_PLUGIN_HEADLESS)
        target_compile_definitions(${target} PUBLIC JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1)
    endif()
    target_link_libraries(${target}
        PRIVATE
            flowstate_session
            FlowstateUi
            FlowstateLibrary
            juce::juce_audio_utils
            juce::juce_gui_extra
        PUBLIC
            juce::juce_recommended_config_flags)
    if(FLOWSTATE_PLUGIN_LTO)
        target_link_libraries(${target} PUBLIC juce::juce_recommended_lto_flags)
    endif()
    flowstate_plugin_warnings(${target})
endfunction()

if(FLOWSTATE_PLUGIN_HEADLESS OR NOT WIN32)
    set(_needs_webview2 FALSE)
else()
    set(_needs_webview2 TRUE)
endif()

juce_add_plugin(Flowstate
    PRODUCT_NAME "Flowstate"
    COMPANY_NAME "Flowstate"
    COMPANY_WEBSITE "https://flowstate.invalid"
    BUNDLE_ID "com.flowstate.instrument"
    PLUGIN_MANUFACTURER_CODE Flws
    PLUGIN_CODE Fls2
    FORMATS ${_instrument_formats}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    NEEDS_WEBVIEW2 ${_needs_webview2}
    VST3_CATEGORIES Instrument Generator
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    COPY_PLUGIN_AFTER_BUILD FALSE)
flowstate_configure_plugin(Flowstate)

juce_add_plugin(FlowstateMidiFx
    PRODUCT_NAME "Flowstate MIDI FX"
    COMPANY_NAME "Flowstate"
    COMPANY_WEBSITE "https://flowstate.invalid"
    BUNDLE_ID "com.flowstate.midifx"
    PLUGIN_MANUFACTURER_CODE Flws
    PLUGIN_CODE Flm2
    FORMATS ${_midifx_formats}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT TRUE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    NEEDS_WEBVIEW2 ${_needs_webview2}
    VST3_CATEGORIES Fx Tools
    AU_MAIN_TYPE kAudioUnitType_MIDIProcessor
    COPY_PLUGIN_AFTER_BUILD FALSE)
flowstate_configure_plugin(FlowstateMidiFx)

# ------------------------------------------------------------------------------------------------
# Tests
if(NOT FLOWSTATE_PLUGIN_BUILD_TESTS)
    return()
endif()

# Processor-level tests: compiles the real processor (with the headless editor) into a console app
# and drives it without a host: host sync, capture, state, bridge, editor lifetime. JUCE keeps
# modules private to the plugin's shared code, so the test builds the sources itself and defines
# the instrument's JucePlugin_* macros.
juce_add_console_app(flowstate_plugin_tests PRODUCT_NAME "Flowstate Plugin Tests")
target_sources(flowstate_plugin_tests PRIVATE
    tests/processor_test.cpp src/PluginProcessor.cpp src/MidiFiles.cpp src/HeadlessEditor.cpp
    "${_build_id_source}")
target_include_directories(flowstate_plugin_tests PRIVATE src)
target_include_directories(flowstate_plugin_tests SYSTEM PRIVATE ${doctest_SOURCE_DIR})
target_compile_definitions(flowstate_plugin_tests PRIVATE
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JucePlugin_Name="Flowstate"
    JucePlugin_IsSynth=1
    JucePlugin_IsMidiEffect=0
    FLOWSTATE_CORE_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../core/tests/fixtures")
target_link_libraries(flowstate_plugin_tests PRIVATE
    flowstate_session
    FlowstateLibrary
    juce::juce_audio_utils
    juce::juce_recommended_config_flags)
flowstate_plugin_warnings(flowstate_plugin_tests)
add_test(NAME processor COMMAND flowstate_plugin_tests)

# Host smoke: loads the built VST3s through JUCE's VST3 host.
juce_add_console_app(flowstate_host_smoke PRODUCT_NAME "Flowstate Host Smoke")
target_sources(flowstate_host_smoke PRIVATE tests/host_smoke.cpp)
target_compile_definitions(flowstate_host_smoke PRIVATE
    JUCE_PLUGINHOST_VST3=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_STANDALONE_APPLICATION=1
    JUCE_DISPLAY_SPLASH_SCREEN=0
    FLOWSTATE_CORE_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../core/tests/fixtures")
target_link_libraries(flowstate_host_smoke PRIVATE
    juce::juce_audio_processors
    juce::juce_recommended_config_flags)
flowstate_plugin_warnings(flowstate_host_smoke)
add_dependencies(flowstate_host_smoke Flowstate_VST3 FlowstateMidiFx_VST3)
add_test(NAME host_smoke
         COMMAND flowstate_host_smoke
                 "$<GENEX_EVAL:$<TARGET_PROPERTY:Flowstate_VST3,JUCE_PLUGIN_ARTEFACT_FILE>>"
                 "$<GENEX_EVAL:$<TARGET_PROPERTY:FlowstateMidiFx_VST3,JUCE_PLUGIN_ARTEFACT_FILE>>")
