cmake_minimum_required(VERSION 3.22)
include(${CMAKE_CURRENT_SOURCE_DIR}/../Shared/RememberVersion.cmake)
project(RememberApp VERSION ${REMEMBER_NUMERIC_VERSION})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(${CMAKE_CURRENT_SOURCE_DIR}/../Shared/RememberSanitizers.cmake)

remember_generate_build_info()

set(JUCE_PATH "E:/JUCE" CACHE PATH "Path to JUCE root")
add_subdirectory(${JUCE_PATH} JUCE EXCLUDE_FROM_ALL)
include(${CMAKE_CURRENT_SOURCE_DIR}/../Shared/RememberFonts.cmake)

juce_add_binary_data(RememberAudioData HEADER_NAME RememberAudioData.h NAMESPACE RememberAudioData
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../Assets/Audio/Metronome4.wav)
juce_add_binary_data(RememberIconData HEADER_NAME RememberIconData.h NAMESPACE RememberIconData
    SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/../Imagenes/Icono de casete en forma de R.png")

juce_add_gui_app(RememberApp
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/../Imagenes/Icono de casete en forma de R.png"
    COMPANY_NAME "Martin Garcia Pacheco"
    BUNDLE_ID    "com.martingarciapacheco.remember.browser"
    PRODUCT_NAME "Remember"
    VERSION      "${REMEMBER_NUMERIC_VERSION}"
)

target_sources(RememberApp PRIVATE
    Source/Main.cpp
    Source/AppShell.cpp
    Source/MainComponent.cpp
    Source/OfflineRendering.cpp
    Source/OfflineRenderTiming.cpp
    Source/OfflineRenderingDialog.cpp
    Source/PreviewSelection.cpp
    Source/ProjectScanner.cpp
    Source/LibraryWatch.cpp
    Source/WaveformPlayerComponent.cpp
    Source/ReadAheadSource.cpp
    Source/MetronomeSource.cpp
    Source/WaveformEnvelope.cpp
    Source/ProjectListComponent.cpp
    Source/ProjectListWorkflow.cpp
    Source/UsedSounds.cpp
    Source/UsedSoundsDialog.cpp
    Source/ProjectStatistics.cpp
    Source/StatisticsDialog.cpp
    Source/SettingsDialog.cpp
    Source/TagsEditorDialog.cpp
    Source/RecoveryChoiceDialog.cpp
    Source/FoldersSidebarComponent.cpp
    ../Shared/ProjectIdentity.cpp
    ../Shared/PreviewAudio.cpp ../Shared/PreviewCompaction.cpp
    ../Shared/LibraryStore.cpp
    ../Shared/BackgroundRetirement.cpp ../Shared/RememberLanguage.cpp
)

target_compile_definitions(RememberApp PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:RememberApp,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:RememberApp,JUCE_VERSION>"
)

juce_generate_juce_header(RememberApp)
if(MSVC)
    target_compile_options(RememberApp PRIVATE /utf-8)
endif()
remember_link_fonts(RememberApp)
if(NOT APPLE)
    add_custom_command(TARGET RememberApp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/../Assets/Fonts/Inter/LICENSE.txt"
            "$<TARGET_FILE_DIR:RememberApp>/Inter-LICENSE.txt")
endif()

target_include_directories(RememberApp PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)

target_link_libraries(RememberApp PRIVATE
    RememberIconData
    RememberAudioData
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_utils
    juce::juce_core
    juce::juce_cryptography
    juce::juce_dsp
    juce::juce_data_structures
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)
if(WIN32)
    target_link_libraries(RememberApp PRIVATE bcrypt comctl32)
endif()
if(NOT REMEMBER_ENABLE_ASAN)
    target_link_libraries(RememberApp PRIVATE juce::juce_recommended_lto_flags)
endif()
remember_copy_asan_runtime(RememberApp)

option(REMEMBER_BUILD_TESTS "Build Remember app regression tests" OFF)
if(REMEMBER_BUILD_TESTS)
    enable_testing()
    juce_add_console_app(RememberAppTests PRODUCT_NAME "Remember app regression tests")
    target_sources(RememberAppTests PRIVATE
        ../Tests/AppTests.cpp ../Tests/PlaybackTests.cpp ../Tests/DeviceTests.cpp
        Source/OfflineRendering.cpp ../Tests/OfflineRenderingTests.cpp
        Source/OfflineRenderTiming.cpp Source/OfflineRenderingDialog.cpp
        Source/PreviewSelection.cpp
        ../Tests/PreviewSelectionTests.cpp
        ../Tests/WaveformTests.cpp
        ../Tests/LibraryTests.cpp ../Tests/RecoveryTests.cpp ../Tests/AtomicJsonTests.cpp
        ../Tests/LibraryWatchTests.cpp
        ../Tests/UndoTests.cpp
        ../Tests/UiTests.cpp Source/FoldersSidebarComponent.cpp Source/MainComponent.cpp
        ../Tests/RecoveryUiTests.cpp ../Tests/StartupTests.cpp
        ../Tests/ShellTests.cpp ../Tests/WorkflowTests.cpp Source/AppShell.cpp
        Source/WaveformPlayerComponent.cpp Source/ProjectScanner.cpp Source/LibraryWatch.cpp
        Source/ProjectListComponent.cpp Source/ProjectListWorkflow.cpp Source/TagsEditorDialog.cpp
        Source/RecoveryChoiceDialog.cpp
        Source/UsedSounds.cpp Source/UsedSoundsDialog.cpp ../Tests/UsedSoundsTests.cpp
        Source/ProjectStatistics.cpp Source/StatisticsDialog.cpp ../Tests/StatisticsTests.cpp
        Source/SettingsDialog.cpp
        Source/ReadAheadSource.cpp Source/MetronomeSource.cpp Source/WaveformEnvelope.cpp
        ../Shared/PreviewAudio.cpp ../Shared/PreviewCompaction.cpp ../Shared/LibraryStore.cpp ../Shared/ProjectIdentity.cpp ../Shared/BackgroundRetirement.cpp ../Shared/RememberLanguage.cpp)
    target_compile_definitions(RememberAppTests PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(RememberAppTests PRIVATE
        RememberIconData RememberAudioData juce::juce_audio_utils juce::juce_gui_extra juce::juce_dsp juce::juce_cryptography juce::juce_recommended_config_flags)
    if(WIN32)
        target_link_libraries(RememberAppTests PRIVATE bcrypt comctl32)
    endif()
    juce_generate_juce_header(RememberAppTests)
    if(MSVC)
        target_compile_options(RememberAppTests PRIVATE /utf-8)
    endif()
    remember_link_fonts(RememberAppTests)
    target_include_directories(RememberAppTests PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
    remember_copy_asan_runtime(RememberAppTests)
    add_test(NAME RememberAppRegression COMMAND RememberAppTests)
    add_test(NAME RememberWaveforms COMMAND RememberAppTests --waveforms)
    add_test(NAME RememberOfflineRendering COMMAND RememberAppTests --offline-rendering)
    add_test(NAME RememberSelectedPreviews COMMAND RememberAppTests --selected-previews)
    set_tests_properties(RememberSelectedPreviews PROPERTIES TIMEOUT 90)
    set_tests_properties(RememberOfflineRendering PROPERTIES TIMEOUT 90)
    set_tests_properties(RememberWaveforms PROPERTIES TIMEOUT 90)
    add_test(NAME RememberContextUndo COMMAND RememberAppTests --undo)
    set_tests_properties(RememberContextUndo PROPERTIES TIMEOUT 90)
    add_test(NAME RememberUsedSounds COMMAND RememberAppTests --used-sounds)
    add_test(NAME RememberStatistics COMMAND RememberAppTests --statistics)
    set_tests_properties(RememberStatistics PROPERTIES TIMEOUT 90)
    set_tests_properties(RememberUsedSounds PROPERTIES TIMEOUT 90)
    add_test(NAME RememberFlStudioBackups COMMAND RememberAppTests --fl-studio-backups)
    add_test(NAME RememberDawBackups COMMAND RememberAppTests --daw-backups)
    set_tests_properties(RememberDawBackups PROPERTIES TIMEOUT 30)
    set_tests_properties(RememberFlStudioBackups PROPERTIES TIMEOUT 30)
    add_test(NAME RememberAppPlayback COMMAND RememberAppTests --playback)
    set_tests_properties(RememberAppPlayback PROPERTIES TIMEOUT 90)
    add_test(NAME RememberAppWorkflow COMMAND RememberAppTests --workflow)
    set_tests_properties(RememberAppWorkflow PROPERTIES TIMEOUT 90)
    add_test(NAME RememberAppFrontend COMMAND RememberAppTests --frontend)
    set_tests_properties(RememberAppFrontend PROPERTIES TIMEOUT 90)
    set_tests_properties(RememberAppRegression PROPERTIES TIMEOUT 300)
    add_test(NAME RememberAppStorageRecovery COMMAND RememberAppTests --storage-recovery)
    set_tests_properties(RememberAppStorageRecovery PROPERTIES TIMEOUT 300)
    add_test(NAME RememberLibraryBenchmark COMMAND RememberAppTests --library-benchmark)
    set_tests_properties(RememberLibraryBenchmark PROPERTIES TIMEOUT 600 LABELS benchmark)
    add_test(NAME RememberAppColdStartup COMMAND RememberAppTests --cold-startup)
    set_tests_properties(RememberAppColdStartup PROPERTIES TIMEOUT 600 LABELS benchmark)
    if(WIN32)
        add_test(NAME RememberAppShell COMMAND RememberAppTests --shell)
        set_tests_properties(RememberAppShell PROPERTIES TIMEOUT 240 LABELS native)
        # Explorer registration requires an interactive Windows session. Keep
        # this acceptance gate explicit on machines that provide that session.
        option(REMEMBER_TEST_NATIVE_TRAY "Test the production tray against interactive Explorer" OFF)
        if(REMEMBER_TEST_NATIVE_TRAY)
            add_test(NAME RememberAppTray COMMAND RememberAppTests --shell-tray)
            set_tests_properties(RememberAppTray PROPERTIES TIMEOUT 120 LABELS native)
        endif()
    endif()
    include(${CMAKE_CURRENT_SOURCE_DIR}/../Shared/RememberTestWorkspace.cmake)
    remember_use_test_workspace()
endif()
