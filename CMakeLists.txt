cmake_minimum_required(VERSION 3.22)

project(Asam VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

# Build a universal binary with: cmake -B build -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
option(ASAM_BUILD_TESTS "Build the offline render and UI snapshot tool" ON)

add_subdirectory(ThirdParty/JUCE)

juce_add_plugin(Asam
    COMPANY_NAME "21 Strive"
    COMPANY_WEBSITE "https://21strive.com"
    BUNDLE_ID com.21strive.asam
    PLUGIN_MANUFACTURER_CODE Strv
    PLUGIN_CODE Asm6
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "Asam"
    DESCRIPTION "Distorted acid bassline synth with a live step sequencer"
    # Drawn by Resources/Icon/asam-app-icon.swift. JUCE makes the .icns when CMake configures.
    ICON_BIG Resources/Icon/asam-app-icon.png
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD TRUE
    VST3_CATEGORIES Instrument Synth
    AU_MAIN_TYPE kAudioUnitType_MusicDevice)

juce_add_binary_data(AsamAssets
    HEADER_NAME AsamAssets.h
    NAMESPACE AsamAssets
    SOURCES
        Resources/Fonts/Geist-Regular.ttf
        Resources/Fonts/Geist-Medium.ttf
        Resources/Fonts/Geist-SemiBold.ttf
        Resources/Fonts/GeistMono-Regular.ttf
        Resources/Fonts/GeistMono-Medium.ttf
        Resources/Fonts/GeistMono-SemiBold.ttf
        Resources/Fonts/GeistPixel-Circle.ttf)

set(ASAM_CORE_SOURCES
    Source/Params.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/SoundPreset.cpp
    Source/seq/PatternBank.cpp
    Source/ui/RapiTheme.cpp
    Source/ui/RapiIcons.cpp
    Source/ui/RapiLookAndFeel.cpp
    Source/ui/RapiWidgets.cpp
    Source/ui/StepGrid.cpp)

target_sources(Asam PRIVATE ${ASAM_CORE_SOURCES})

# Our own standalone application: native title bar, MIDI inputs opened automatically,
# and the audio settings reachable from inside the Rapi interface.
target_sources(Asam_Standalone PRIVATE Source/StandaloneApp.cpp)

target_include_directories(Asam PRIVATE Source)

target_compile_definitions(Asam
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1)

target_link_libraries(Asam
    PRIVATE
        AsamAssets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(ASAM_BUILD_TESTS)
    # Console tool: renders the synth offline to WAV, checks the DSP numerically and
    # snapshots the editor to PNG. Not shipped.
    juce_add_console_app(AsamCheck PRODUCT_NAME "AsamCheck")
    target_sources(AsamCheck PRIVATE Tests/AsamCheck.cpp ${ASAM_CORE_SOURCES})
    target_include_directories(AsamCheck PRIVATE Source)
    target_compile_definitions(AsamCheck PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JucePlugin_Name="Asam"
        ASAM_CHECK_TOOL=1)
    target_link_libraries(AsamCheck
        PRIVATE
            AsamAssets
            juce::juce_audio_utils
            juce::juce_dsp
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endif()
