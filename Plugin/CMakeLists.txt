cmake_minimum_required(VERSION 3.22)
project(RememberPlugin VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Version + commit hash injected at configure time ─────────────────────────
set(REMEMBER_VERSION "1.0.0-beta3" CACHE STRING "Product version string")

# Look up the short HEAD commit; empty string if not in a git work-tree
execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
    OUTPUT_VARIABLE REMEMBER_GIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT REMEMBER_GIT_HASH)
    set(REMEMBER_GIT_HASH "unknown")
endif()
string(TIMESTAMP REMEMBER_BUILD_DATE "%Y-%m-%d")

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/BuildInfo.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/BuildInfo.h
    @ONLY
)

# Adjust this path if JUCE is installed elsewhere
set(JUCE_PATH "E:/JUCE" CACHE PATH "Path to JUCE root")
add_subdirectory(${JUCE_PATH} JUCE EXCLUDE_FROM_ALL)

option(REMEMBER_COPY_PLUGIN_AFTER_BUILD "Install the VST3 after building" ON)
juce_add_plugin(RememberPlugin
    PLUGIN_NAME             "Remember"
    PLUGIN_MANUFACTURER_CODE "Mnjl"
    PLUGIN_CODE             "Rmbr"
    FORMATS                 VST3
    IS_SYNTH                FALSE
    NEEDS_MIDI_INPUT        FALSE
    NEEDS_MIDI_OUTPUT       FALSE
    IS_MIDI_EFFECT          FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD ${REMEMBER_COPY_PLUGIN_AFTER_BUILD}
    VST3_COPY_DIR           "C:/Program Files/Common Files/VST3"
    PRODUCT_NAME            "Remember"
)

target_sources(RememberPlugin PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/RecordingEngine.cpp
    Source/KeyDetector.cpp
    Source/BpmDetector.cpp
    Source/SettingsManager.cpp
    ../Shared/ProjectIdentity.cpp
)

target_compile_definitions(RememberPlugin PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
)

juce_generate_juce_header(RememberPlugin)

target_include_directories(RememberPlugin PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)

target_link_libraries(RememberPlugin PRIVATE
    juce::juce_audio_basics
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_core
    juce::juce_cryptography
    juce::juce_data_structures
    juce::juce_dsp
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

option(REMEMBER_BUILD_TESTS "Build Remember regression tests" OFF)
if(REMEMBER_BUILD_TESTS)
    enable_testing()
    juce_add_console_app(RememberTests PRODUCT_NAME "Remember regression tests")
    target_sources(RememberTests PRIVATE ../Tests/RememberTests.cpp ../App/Source/ProjectScanner.cpp)
    target_link_libraries(RememberTests PRIVATE RememberPlugin
        juce::juce_audio_processors juce::juce_audio_formats juce::juce_dsp
        juce::juce_cryptography juce::juce_recommended_config_flags)
    juce_generate_juce_header(RememberTests)
    add_test(NAME RememberRegression COMMAND RememberTests)
    if(WIN32)
        add_test(NAME RememberHostLoading COMMAND RememberTests --host-loading)
        set_tests_properties(RememberHostLoading PROPERTIES TIMEOUT 30)
    endif()
endif()
