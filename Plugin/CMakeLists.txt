cmake_minimum_required(VERSION 3.22)
project(RememberPlugin VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Adjust this path if JUCE is installed elsewhere
set(JUCE_PATH "E:/JUCE" CACHE PATH "Path to JUCE root")
add_subdirectory(${JUCE_PATH} JUCE EXCLUDE_FROM_ALL)

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
    COPY_PLUGIN_AFTER_BUILD TRUE
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
)

target_compile_definitions(RememberPlugin PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
)

juce_generate_juce_header(RememberPlugin)

target_link_libraries(RememberPlugin PRIVATE
    juce::juce_audio_basics
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_core
    juce::juce_data_structures
    juce::juce_dsp
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)
