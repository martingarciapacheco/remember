cmake_minimum_required(VERSION 3.22)
project(RememberApp VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(JUCE_PATH "E:/JUCE" CACHE PATH "Path to JUCE root")
add_subdirectory(${JUCE_PATH} JUCE EXCLUDE_FROM_ALL)

juce_add_gui_app(RememberApp
    PRODUCT_NAME "Remember"
    VERSION      "1.0.0"
)

target_sources(RememberApp PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/ProjectScanner.cpp
    Source/WaveformPlayerComponent.cpp
    Source/ProjectListComponent.cpp
    Source/SettingsDialog.cpp
    Source/TagsEditorDialog.cpp
    Source/FoldersSidebarComponent.cpp
)

target_compile_definitions(RememberApp PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:RememberApp,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:RememberApp,JUCE_VERSION>"
)

juce_generate_juce_header(RememberApp)

target_link_libraries(RememberApp PRIVATE
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_utils
    juce::juce_core
    juce::juce_data_structures
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)
