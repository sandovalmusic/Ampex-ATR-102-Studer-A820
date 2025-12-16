cmake_minimum_required(VERSION 3.22)

project(TapeMachinePlugin VERSION 1.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Use static runtime on Windows for distribution (no MSVC runtime dependency)
# This is CRITICAL - without this, the plugin won't load on machines without
# Visual C++ Redistributable installed
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    # Additional MSVC flags for compatibility
    add_compile_options(/EHsc)  # Enable C++ exception handling
endif()

# MinGW also needs static linking
if(MINGW)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static-libgcc -static-libstdc++")
endif()

# Fetch JUCE (8.0.4 for macOS 15 compatibility)
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(JUCE)

# Determine plugin formats and copy directories based on platform
if(APPLE)
    set(PLUGIN_FORMATS VST3 AU)
    set(VST3_COPY_DEST "$ENV{HOME}/Desktop")
    set(AU_COPY_DEST "$ENV{HOME}/Desktop")
elseif(WIN32)
    set(PLUGIN_FORMATS VST3)
    # On Windows, copy to user's Desktop using USERPROFILE
    set(VST3_COPY_DEST "$ENV{USERPROFILE}/Desktop")
else()
    set(PLUGIN_FORMATS VST3)
    set(VST3_COPY_DEST "$ENV{HOME}/Desktop")
endif()

# Ampex ATR-102 / Studer A820 Tape Emulation
juce_add_plugin(TapeMachinePlugin
    COMPANY_NAME "AudioBengineer"
    PLUGIN_MANUFACTURER_CODE AuBn
    PLUGIN_CODE AmSt
    FORMATS ${PLUGIN_FORMATS}
    PRODUCT_NAME "Ampex ATR-102 - Studer A820"
    DESCRIPTION "Physics-based tape saturation emulation"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    VST3_COPY_DIR "${VST3_COPY_DEST}"
    AU_COPY_DIR "${AU_COPY_DEST}"
    VST3_CATEGORIES "Fx" "Distortion"
    AU_MAIN_TYPE kAudioUnitType_Effect
)

# Source files
target_sources(TapeMachinePlugin PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    ../Source/DSP/HybridTapeProcessor.cpp
    ../Source/DSP/BiasShielding.cpp
    ../Source/DSP/MachineEQ.cpp
)

# Include directories
target_include_directories(TapeMachinePlugin PRIVATE
    Source
    ../Source
)

# Link JUCE modules
target_link_libraries(TapeMachinePlugin PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

# Compiler definitions
target_compile_definitions(TapeMachinePlugin PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
)
