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
    Source/Core/Originality.cpp
    Source/Core/OriginalityData.cpp
    Source/Core/PromptInterpreter.cpp
    Source/Core/QualityCheck.cpp
    Source/Core/RequestCheck.cpp
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

    # The Inter typeface (SIL Open Font License, Resources/Fonts/Inter-OFL.txt) for the editor.
    juce_add_binary_data (PromptMIDIData SOURCES
        Resources/Fonts/Inter-Regular.ttf
        Resources/Fonts/Inter-Medium.ttf
        Resources/Fonts/Inter-SemiBold.ttf)

    # TinySoundFont (MIT, ThirdParty/TinySoundFont/LICENSE) plays the preview's SoundFont.
    add_library (promptmidi_tsf STATIC ThirdParty/TinySoundFont/tsf.c)
    target_include_directories (promptmidi_tsf PUBLIC ThirdParty/TinySoundFont)

    if (NOT MSVC)
        target_link_libraries (promptmidi_tsf PRIVATE m)
    endif()

    # The preview's instruments: GeneralUser GS (Resources/SoundFont/GeneralUser-GS-LICENSE.txt),
    # copied into each bundle's Contents/Resources (or next to the app / test tool).
    set (PROMPTMIDI_SOUNDFONT_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/Resources/SoundFont/GeneralUser-GS.sf2"
        "${CMAKE_CURRENT_SOURCE_DIR}/Resources/SoundFont/GeneralUser-GS-LICENSE.txt")

    function (promptmidi_copy_soundfont target destination)
        add_custom_command (TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${destination}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PROMPTMIDI_SOUNDFONT_FILES} "${destination}"
            VERBATIM)
    endfunction()

    set (PROMPTMIDI_PLUGIN_SOURCES
        Source/Plugin/ClaudeClient.cpp
        Source/Plugin/InstrumentPicker.cpp
        Source/Plugin/PluginEditor.cpp
        Source/Plugin/PluginProcessor.cpp
        Source/Plugin/PresetIO.cpp
        Source/Plugin/PreviewEngine.cpp
        Source/Plugin/PreviewSynth.cpp
        Source/Plugin/Settings.cpp
        Source/Plugin/SpecJson.cpp
        Source/Plugin/Timeline.cpp)

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
            promptmidi_tsf
            PromptMIDIData
            juce::juce_audio_utils
            juce::juce_audio_processors
            juce::juce_gui_extra
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

    get_target_property (PROMPTMIDI_VST3_BUNDLE PromptMIDI_VST3 JUCE_PLUGIN_ARTEFACT_FILE)
    promptmidi_copy_soundfont (PromptMIDI_VST3 "${PROMPTMIDI_VST3_BUNDLE}/Contents/Resources")
    get_target_property (PROMPTMIDI_APP PromptMIDI_Standalone JUCE_PLUGIN_ARTEFACT_FILE)

    if (APPLE)
        promptmidi_copy_soundfont (PromptMIDI_Standalone "${PROMPTMIDI_APP}/Contents/Resources")
        get_target_property (PROMPTMIDI_AU_BUNDLE PromptMIDI_AU JUCE_PLUGIN_ARTEFACT_FILE)
        promptmidi_copy_soundfont (PromptMIDI_AU "${PROMPTMIDI_AU_BUNDLE}/Contents/Resources")
    else()
        promptmidi_copy_soundfont (PromptMIDI_Standalone "$<TARGET_FILE_DIR:PromptMIDI_Standalone>")
    endif()

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
        target_link_libraries (promptmidi_ui_snapshot PRIVATE promptmidi_core promptmidi_tsf PromptMIDIData juce::juce_audio_utils juce::juce_audio_processors juce::juce_gui_extra
                               PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

        # Renders the in-plugin preview to a WAV: promptmidi_render_preview out.wav "prompt" [seconds] [seed] [sketch]
        juce_add_console_app (promptmidi_render_preview PRODUCT_NAME "promptmidi_render_preview" NEEDS_CURL TRUE)
        target_sources (promptmidi_render_preview PRIVATE Tools/render_preview.cpp ${PROMPTMIDI_PLUGIN_SOURCES})
        target_include_directories (promptmidi_render_preview PRIVATE Source/Plugin)
        target_compile_definitions (promptmidi_render_preview PRIVATE JucePlugin_Name="PromptMIDI" JUCE_WEB_BROWSER=0)
        target_link_libraries (promptmidi_render_preview PRIVATE promptmidi_core promptmidi_tsf PromptMIDIData juce::juce_audio_utils
                               juce::juce_audio_processors juce::juce_gui_extra
                               PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
        promptmidi_copy_soundfont (promptmidi_render_preview "$<TARGET_FILE_DIR:promptmidi_render_preview>")

        # The plug-in in-process: editing, variation, instruments, preview containment, the editor views.
        juce_add_console_app (promptmidi_editor_tests PRODUCT_NAME "promptmidi_editor_tests" NEEDS_CURL TRUE)
        target_sources (promptmidi_editor_tests PRIVATE Tests/EditorTests.cpp ${PROMPTMIDI_PLUGIN_SOURCES})
        target_include_directories (promptmidi_editor_tests PRIVATE Source/Plugin)
        target_compile_definitions (promptmidi_editor_tests PRIVATE JucePlugin_Name="PromptMIDI" JUCE_WEB_BROWSER=0)
        target_link_libraries (promptmidi_editor_tests PRIVATE promptmidi_core promptmidi_tsf PromptMIDIData juce::juce_audio_utils juce::juce_audio_processors juce::juce_gui_extra
                               PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
        promptmidi_copy_soundfont (promptmidi_editor_tests "$<TARGET_FILE_DIR:promptmidi_editor_tests>")
        add_test (NAME editor_tests COMMAND promptmidi_editor_tests)
    endif()
endif()
