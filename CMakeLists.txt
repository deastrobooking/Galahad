cmake_minimum_required(VERSION 3.21)
project(Galahad VERSION 0.1.2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(galahad_core
    src/AlgorithmicSequencer.cpp
    src/MidiLfo.cpp
    src/MidiRouting.cpp
    src/MidiToolsEngine.cpp
    src/OscCodec.cpp
    src/OscUdpTransport.cpp
    src/LiveController.cpp
    src/RealtimeEvent.cpp
)

target_include_directories(galahad_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(WIN32)
    target_link_libraries(galahad_core PUBLIC ws2_32)
endif()

option(GALAHAD_BUILD_EXAMPLE "Build example executable" ON)
option(GALAHAD_BUILD_JUCE_MIDI_TOOLS "Build optional JUCE MIDI tools plugin" OFF)
option(GALAHAD_COPY_PLUGIN_AFTER_BUILD "Copy built plugin artefacts into local plugin folders" OFF)

if(APPLE)
    set(GALAHAD_MACOS_PLUGIN_COPY_ROOT "$ENV{HOME}/Library/Audio/Plug-Ins" CACHE PATH
        "macOS plugin folder used when GALAHAD_COPY_PLUGIN_AFTER_BUILD is enabled")
endif()

if(GALAHAD_BUILD_EXAMPLE)
    add_executable(galahad_cli examples/cli/main.cpp)
    target_link_libraries(galahad_cli PRIVATE galahad_core)
endif()

if(GALAHAD_BUILD_JUCE_MIDI_TOOLS)
    include(FetchContent)

    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
    )
    FetchContent_MakeAvailable(JUCE)

    add_library(galahad_juce_midi STATIC
        src/juce/MidiProtocol.cpp
    )

    target_include_directories(galahad_juce_midi
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    target_link_libraries(galahad_juce_midi
        PUBLIC
            juce::juce_audio_basics
            juce::juce_core
    )

    juce_add_plugin(GalahadMidiTools
        VERSION                     ${PROJECT_VERSION}
        COMPANY_NAME                "Galahad"
        BUNDLE_ID                   "com.galahad.miditools"
        PLUGIN_MANUFACTURER_CODE    GLHD
        PLUGIN_CODE                 GLM1
        FORMATS                     Standalone VST3
        IS_SYNTH                    TRUE
        NEEDS_MIDI_INPUT            TRUE
        NEEDS_MIDI_OUTPUT           TRUE
        IS_MIDI_EFFECT              FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD     ${GALAHAD_COPY_PLUGIN_AFTER_BUILD}
        VST3_COPY_DIR               "${GALAHAD_MACOS_PLUGIN_COPY_ROOT}/VST3"
        PRODUCT_NAME                "Galahad MIDI Tools"
        DESCRIPTION                 "Silent MIDI source and session-grid tools for Ableton bridge development"
        VST3_CATEGORIES             "Synth|Tools"
    )

    juce_generate_juce_header(GalahadMidiTools)

    target_sources(GalahadMidiTools
        PRIVATE
            examples/juce-plugin/GalahadMidiToolsProcessor.cpp
            examples/juce-plugin/GalahadMidiToolsProcessor.h
    )

    target_link_libraries(GalahadMidiTools
        PRIVATE
            galahad_core
            galahad_juce_midi
            juce::juce_audio_devices
            juce::juce_audio_plugin_client
            juce::juce_audio_processors
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags
    )

    target_compile_definitions(GalahadMidiTools
        PUBLIC
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_VST3_CAN_REPLACE_VST2=0
    )

    if(APPLE)
        add_custom_command(TARGET GalahadMidiTools_VST3 POST_BUILD
            COMMAND /usr/bin/codesign --force --sign - "$<TARGET_BUNDLE_DIR:GalahadMidiTools_VST3>"
            COMMENT "Ad-hoc signing Galahad MIDI Tools VST3 bundle")

        add_custom_command(TARGET GalahadMidiTools_Standalone POST_BUILD
            COMMAND /usr/bin/codesign --force --sign - "$<TARGET_BUNDLE_DIR:GalahadMidiTools_Standalone>"
            COMMENT "Ad-hoc signing Galahad MIDI Tools Standalone bundle")
    endif()
endif()
