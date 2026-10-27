cmake_minimum_required(VERSION 3.22)

project(Murli VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")

# JUCE: use a local checkout with -DMURLI_JUCE_DIR=/path/to/JUCE, otherwise fetch it.
set(MURLI_JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout (optional)")
option(MURLI_COPY_AFTER_BUILD "Install plug-ins into the user's plug-in folders after building" OFF)

if(MURLI_JUCE_DIR)
    add_subdirectory(${MURLI_JUCE_DIR} JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(MURLI_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND MURLI_FORMATS AU)
endif()

juce_add_plugin(Murli
    COMPANY_NAME "Murli"
    BUNDLE_ID "com.dbajpeyi.murli"
    PLUGIN_MANUFACTURER_CODE Dbjp
    PLUGIN_CODE Mrli
    PRODUCT_NAME "Murli"
    DESCRIPTION "Electrostatic reed piano modelled on the 200-series"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD ${MURLI_COPY_AFTER_BUILD}
    VST3_CATEGORIES Instrument Synth
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    HARDENED_RUNTIME_ENABLED TRUE
    FORMATS ${MURLI_FORMATS})

juce_add_binary_data(MurliAssets SOURCES
    Resources/fonts/Pacifico-Regular.ttf
    Resources/fonts/BebasNeue-Regular.ttf)

target_sources(Murli PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/gui/RetroLookAndFeel.cpp
    Source/gui/Widgets.cpp
    Source/gui/RearPanel.cpp
    Source/StandaloneApp.cpp)

target_include_directories(Murli PRIVATE Source)

target_compile_definitions(Murli PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1)

target_link_libraries(Murli
    PRIVATE
        MurliAssets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
