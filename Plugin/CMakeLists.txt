cmake_minimum_required(VERSION 3.22)
include(${CMAKE_CURRENT_SOURCE_DIR}/../Shared/RememberVersion.cmake)
project(RememberPlugin VERSION ${REMEMBER_NUMERIC_VERSION})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(${CMAKE_CURRENT_SOURCE_DIR}/../Shared/RememberSanitizers.cmake)

remember_generate_build_info()

# Adjust this path if JUCE is installed elsewhere
set(JUCE_PATH "E:/JUCE" CACHE PATH "Path to JUCE root")
add_subdirectory(${JUCE_PATH} JUCE EXCLUDE_FROM_ALL)
include(${CMAKE_CURRENT_SOURCE_DIR}/../Shared/RememberFonts.cmake)

option(REMEMBER_COPY_PLUGIN_AFTER_BUILD "Install the VST3 after building" OFF)
juce_add_plugin(RememberPlugin
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/../Imagenes/Icono de casete en forma de D.png"
    COMPANY_NAME            "Martin Garcia Pacheco"
    COMPANY_WEBSITE         "https://github.com/martingarciapacheco/remember"
    BUNDLE_ID               "com.martingarciapacheco.remember.plugin"
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
    Source/RecordingCore.cpp
    Source/KeyDetector.cpp
    Source/BpmDetector.cpp
    Source/TempoPeriodicity.h
    Source/AnalysisAudio.cpp
    Source/PreviewAnalysis.cpp
    Source/SettingsManager.cpp
    ../Shared/ProjectIdentity.cpp
    ../Shared/LibraryStore.cpp
    ../Shared/BackgroundRetirement.cpp
)

target_compile_definitions(RememberPlugin PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
)

juce_generate_juce_header(RememberPlugin)
remember_link_fonts(RememberPlugin)
if(NOT APPLE)
    add_custom_command(TARGET RememberPlugin_VST3 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:RememberPlugin_VST3>/../Resources"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/../Assets/Fonts/Inter/LICENSE.txt"
            "$<TARGET_FILE_DIR:RememberPlugin_VST3>/../Resources/Inter-LICENSE.txt")
endif()

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
    juce::juce_recommended_warning_flags
)
if(WIN32)
    target_link_libraries(RememberPlugin PRIVATE bcrypt)
endif()
if(NOT REMEMBER_ENABLE_ASAN)
    target_link_libraries(RememberPlugin PRIVATE juce::juce_recommended_lto_flags)
endif()
remember_copy_asan_runtime(RememberPlugin_VST3)
if(TARGET RememberPlugin_vst3_helper)
    remember_copy_asan_runtime(RememberPlugin_vst3_helper)
endif()

option(REMEMBER_BUILD_TESTS "Build Remember regression tests" OFF)
if(REMEMBER_BUILD_TESTS)
    enable_testing()
    juce_add_console_app(RememberTests PRODUCT_NAME "Remember regression tests")
    target_sources(RememberTests PRIVATE ../Tests/RememberTests.cpp ../Tests/StorageTests.cpp ../Tests/RecoveryTests.cpp ../Tests/StressTests.cpp ../Tests/LifecycleTests.cpp ../Tests/ConfigurationTests.cpp ../Tests/AnalysisTests.cpp ../Tests/CaptureMatrixTests.cpp ../App/Source/ProjectScanner.cpp ../App/Source/LibraryWatch.cpp)
    target_link_libraries(RememberTests PRIVATE RememberPlugin
        juce::juce_audio_processors juce::juce_audio_formats juce::juce_dsp
        juce::juce_cryptography juce::juce_recommended_config_flags)
    juce_generate_juce_header(RememberTests)
    remember_link_fonts(RememberTests)
    remember_copy_asan_runtime(RememberTests)
    target_sources(RememberTests PRIVATE ../Tests/AssociationTests.cpp)
    target_sources(RememberTests PRIVATE ../Tests/MusicCorpus.cpp ../Tests/MusicFeatures.cpp)
    target_sources(RememberTests PRIVATE ../Tests/AtomicJsonTests.cpp)
    target_sources(RememberTests PRIVATE ../Tests/TransportTests.cpp)
    target_sources(RememberTests PRIVATE ../Tests/WorkflowCaptureTests.cpp)
    add_test(NAME RememberWorkflowCapture COMMAND RememberTests --workflow-capture)
    set_tests_properties(RememberWorkflowCapture PROPERTIES TIMEOUT 60)
    target_sources(RememberTests PRIVATE ../Tests/PreviewQualityTests.cpp)
    add_test(NAME RememberPreviewQuality COMMAND RememberTests --preview-quality)
    set_tests_properties(RememberPreviewQuality PROPERTIES TIMEOUT 90)
    target_sources(RememberTests PRIVATE ../Tests/ResamplingTests.cpp)
    add_test(NAME RememberResampling COMMAND RememberTests --resampling)
    set_tests_properties(RememberResampling PROPERTIES TIMEOUT 90)
    target_sources(RememberTests PRIVATE ../Tests/FeatureCacheTests.cpp)
    add_test(NAME RememberFeatureCache COMMAND RememberTests --feature-cache)
    set_tests_properties(RememberFeatureCache PROPERTIES TIMEOUT 180)
    add_test(NAME RememberAssociation COMMAND RememberTests --association)
    set_tests_properties(RememberAssociation PROPERTIES TIMEOUT 90)
    add_test(NAME RememberRegression COMMAND RememberTests)
    add_test(NAME RememberPluginFrontend COMMAND RememberTests --frontend)
    set_tests_properties(RememberPluginFrontend PROPERTIES TIMEOUT 60)
    set_tests_properties(RememberRegression PROPERTIES TIMEOUT 60)
    add_test(NAME RememberConfiguration COMMAND RememberTests --configuration)
    set_tests_properties(RememberConfiguration PROPERTIES TIMEOUT 60)
    add_test(NAME RememberStorageRecovery COMMAND RememberTests --storage-recovery)
    set_tests_properties(RememberStorageRecovery PROPERTIES TIMEOUT 300)
    add_test(NAME RememberSustainedCapture COMMAND RememberTests --sustained-capture)
    set_tests_properties(RememberSustainedCapture PROPERTIES TIMEOUT 300 LABELS stress)
    add_test(NAME RememberAnalysis COMMAND RememberTests --analysis)
    set_tests_properties(RememberAnalysis PROPERTIES TIMEOUT 90)
    add_test(NAME RememberCaptureMatrix COMMAND RememberTests --capture-matrix)
    set_tests_properties(RememberCaptureMatrix PROPERTIES TIMEOUT 90)
    if(WIN32)
        find_program(REMEMBER_PWSH_EXECUTABLE NAMES pwsh.exe pwsh
            HINTS "$ENV{ProgramFiles}/PowerShell/7")
        if(NOT REMEMBER_PWSH_EXECUTABLE)
            message(FATAL_ERROR "PowerShell 7.2+ is required for validation isolation tests; set REMEMBER_PWSH_EXECUTABLE")
        endif()
        add_test(NAME RememberValidationIsolation COMMAND "${REMEMBER_PWSH_EXECUTABLE}"
            -NoProfile -File "${CMAKE_CURRENT_SOURCE_DIR}/../Tests/ValidationProfileTests.ps1"
            -TestExecutable "$<TARGET_FILE:RememberTests>")
        set_tests_properties(RememberValidationIsolation PROPERTIES TIMEOUT 60)
        target_link_libraries(RememberTests PRIVATE psapi)
        add_library(RememberLifetimeFixture SHARED ../Tests/RetirementFixture.cpp ../Shared/BackgroundRetirement.cpp)
        remember_copy_asan_runtime(RememberLifetimeFixture)
        add_dependencies(RememberTests RememberLifetimeFixture)
        target_compile_definitions(RememberTests PRIVATE REMEMBER_LIFETIME_FIXTURE_PATH="$<TARGET_FILE:RememberLifetimeFixture>")
        add_test(NAME RememberHostLoading COMMAND RememberTests --host-loading)
        set_tests_properties(RememberHostLoading PROPERTIES TIMEOUT 30)
    endif()
    include(${CMAKE_CURRENT_SOURCE_DIR}/../Shared/RememberTestWorkspace.cmake)
    remember_use_test_workspace()
endif()
