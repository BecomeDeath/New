cmake_minimum_required (VERSION 3.22)

project (PromptMIDI VERSION 1.0.0 LANGUAGES C CXX)

set (CMAKE_CXX_STANDARD 17)
set (CMAKE_CXX_STANDARD_REQUIRED ON)
set (CMAKE_POSITION_INDEPENDENT_CODE ON)

if (APPLE)
    set (CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()

option (PROMPTMIDI_BUILD_PLUGIN "Build the JUCE plug-in (VST3 / Standalone / AU on macOS)" ON)
option (PROMPTMIDI_BUILD_TESTS  "Build the unit tests and the command-line tool" ON)
option (PROMPTMIDI_COPY_PLUGIN  "Copy the VST3 into the system plug-in folder after building" OFF)
set (PROMPTMIDI_JUCE_DIR "" CACHE PATH "Use a local JUCE checkout instead of downloading it")

#===============================================================================
# Engine: plain C++17, no JUCE, shared by the plug-in, the CLI and the tests.
add_library (promptmidi_core STATIC
    Source/Core/Composer.cpp
    Source/Core/CueEngine.cpp
    Source/Core/Learned.cpp
    Source/Core/LearnedData.cpp
    Source/Core/MidiFileWriter.cpp
    Source/Core/MusicTheory.cpp
    Source/Core/PromptInterpreter.cpp
    Source/Core/QualityCheck.cpp
    Source/Core/StyleSpec.cpp)

target_include_directories (promptmidi_core PUBLIC Source/Core)

if (MSVC)
    target_compile_options (promptmidi_core PRIVATE /W4 /utf-8)
else()
    target_compile_options (promptmidi_core PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

if (PROMPTMIDI_BUILD_TESTS)
    enable_testing()

    add_executable (promptmidi-cli Tools/promptmidi_cli.cpp)
    target_link_libraries (promptmidi-cli PRIVATE promptmidi_core)

    add_executable (promptmidi_tests Tests/CoreTests.cpp)
    target_link_libraries (promptmidi_tests PRIVATE promptmidi_core)
    add_test (NAME core_tests COMMAND promptmidi_tests)
endif()

#===============================================================================
# Plug-in
if (PROMPTMIDI_BUILD_PLUGIN)
    if (PROMPTMIDI_JUCE_DIR)
        add_subdirectory ("${PROMPTMIDI_JUCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")
    else()
        include (FetchContent)
        FetchContent_Declare (JUCE
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG        9.0.6
            GIT_SHALLOW    TRUE)
        FetchContent_MakeAvailable (JUCE)
    endif()

    set (PROMPTMIDI_FORMATS VST3 Standalone)

    if (APPLE)
        list (APPEND PROMPTMIDI_FORMATS AU)
    endif()

    juce_add_plugin (PromptMIDI
        COMPANY_NAME                "PromptMIDI"
        COMPANY_WEBSITE             "https://github.com/BecomeDeath/New"
        BUNDLE_ID                   "com.promptmidi.promptmidi"
        PLUGIN_MANUFACTURER_CODE    Pmdi
        PLUGIN_CODE                 Pmd1
        FORMATS                     ${PROMPTMIDI_FORMATS}
        PRODUCT_NAME                "PromptMIDI"
        DESCRIPTION                 "Describe music in words, get MIDI"
        IS_SYNTH                    TRUE
        NEEDS_MIDI_INPUT            TRUE
        NEEDS_MIDI_OUTPUT           TRUE
        IS_MIDI_EFFECT              FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        NEEDS_CURL                  TRUE
        NEEDS_WEB_BROWSER           FALSE
        VST3_CATEGORIES             Instrument Generator
        AU_MAIN_TYPE                kAudioUnitType_MusicDevice
        COPY_PLUGIN_AFTER_BUILD     ${PROMPTMIDI_COPY_PLUGIN})

    set (PROMPTMIDI_PLUGIN_SOURCES
        Source/Plugin/ClaudeClient.cpp
        Source/Plugin/PianoRoll.cpp
        Source/Plugin/PluginEditor.cpp
        Source/Plugin/PluginProcessor.cpp
        Source/Plugin/PreviewSynth.cpp
        Source/Plugin/Settings.cpp
        Source/Plugin/SpecJson.cpp)

    target_sources (PromptMIDI PRIVATE ${PROMPTMIDI_PLUGIN_SOURCES})

    target_include_directories (PromptMIDI PRIVATE Source/Plugin)

    target_compile_definitions (PromptMIDI PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0
        JUCE_STRICT_REFCOUNTEDPOINTER=1)

    if (MSVC)
        target_compile_options (PromptMIDI PRIVATE /utf-8)
    endif()

    target_link_libraries (PromptMIDI
        PRIVATE
            promptmidi_core
            juce::juce_audio_utils
            juce::juce_audio_processors
            juce::juce_gui_extra
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

    # Headless tests for the JSON / Claude request code (only needs juce_core).
    if (PROMPTMIDI_BUILD_TESTS)
        juce_add_console_app (promptmidi_json_tests PRODUCT_NAME "promptmidi_json_tests" NEEDS_CURL TRUE)
        target_sources (promptmidi_json_tests PRIVATE Tests/JsonTests.cpp Source/Plugin/SpecJson.cpp Source/Plugin/ClaudeClient.cpp)
        target_include_directories (promptmidi_json_tests PRIVATE Source/Plugin)
        target_compile_definitions (promptmidi_json_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_STANDALONE_APPLICATION=1)
        target_link_libraries (promptmidi_json_tests PRIVATE promptmidi_core juce::juce_core juce::juce_events
                               PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
        add_test (NAME json_tests COMMAND promptmidi_json_tests)

        # Loads the built VST3 through JUCE's VST3 hosting and plays it.
        juce_add_console_app (promptmidi_host_test PRODUCT_NAME "promptmidi_host_test")
        target_sources (promptmidi_host_test PRIVATE Tests/HostSmokeTest.cpp Source/Plugin/SpecJson.cpp)
        target_include_directories (promptmidi_host_test PRIVATE Source/Plugin)
        target_compile_definitions (promptmidi_host_test PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
        target_link_libraries (promptmidi_host_test PRIVATE promptmidi_core juce::juce_audio_processors
                               PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
        add_dependencies (promptmidi_host_test PromptMIDI_VST3)
        get_target_property (PROMPTMIDI_VST3_PATH PromptMIDI_VST3 JUCE_PLUGIN_ARTEFACT_FILE)
        add_test (NAME vst3_host_test COMMAND promptmidi_host_test "${PROMPTMIDI_VST3_PATH}")

        # Renders the editor offscreen to a PNG: promptmidi_ui_snapshot out.png "prompt"
        juce_add_console_app (promptmidi_ui_snapshot PRODUCT_NAME "promptmidi_ui_snapshot" NEEDS_CURL TRUE)
        target_sources (promptmidi_ui_snapshot PRIVATE Tools/ui_snapshot.cpp ${PROMPTMIDI_PLUGIN_SOURCES})
        target_include_directories (promptmidi_ui_snapshot PRIVATE Source/Plugin)
        target_compile_definitions (promptmidi_ui_snapshot PRIVATE JucePlugin_Name="PromptMIDI" JUCE_WEB_BROWSER=0)
        target_link_libraries (promptmidi_ui_snapshot PRIVATE promptmidi_core juce::juce_audio_utils juce::juce_audio_processors juce::juce_gui_extra
                               PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    endif()
endif()
