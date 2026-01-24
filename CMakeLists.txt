cmake_minimum_required(VERSION 3.22)

project(Villain VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ==============================================================================
# Build options
# ==============================================================================
option(VILLAIN_ENABLE_VST3   "Enable VST3 format" OFF)
option(VILLAIN_ENABLE_AU     "Enable AU format (macOS only)" ON)
option(VILLAIN_ENABLE_CLAP   "Enable CLAP format" OFF)

# JUCE location
set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libs/JUCE" CACHE PATH "Path to JUCE")

# CLAP extensions location
set(CLAP_EXTENSIONS_DIR "" CACHE PATH "Path to clap-juce-extensions root folder")

# ==============================================================================
# JUCE
# ==============================================================================
add_subdirectory("${JUCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")

# ==============================================================================
# Sources
# ==============================================================================
set(VILLAIN_SRC
    src/PluginProcessor.h
    src/PluginProcessor.cpp
    src/PluginEditor.h
    src/PluginEditor.cpp
)

# ==============================================================================
# Assets
# ==============================================================================
juce_add_binary_data(VillainAssets SOURCES
    assets/a1.png
    assets/a2.png
    assets/a3.png
    assets/a4.png
    assets/a5.png
    assets/a6.png
    assets/a7.png
    assets/a8.png
    assets/a9.png
    assets/a10.png
    assets/knob.png
)

# ==============================================================================
# Build formats list
# ==============================================================================
set(_formats "")

if(VILLAIN_ENABLE_VST3)
    list(APPEND _formats VST3)
endif()

if(APPLE AND VILLAIN_ENABLE_AU)
    list(APPEND _formats AU)
endif()

if(VILLAIN_ENABLE_CLAP AND NOT "${CLAP_EXTENSIONS_DIR}" STREQUAL "")
    if(NOT TARGET clap_juce_extensions)
        add_subdirectory("${CLAP_EXTENSIONS_DIR}" "${CMAKE_BINARY_DIR}/clap_juce_extensions_build")
    endif()
    list(APPEND _formats CLAP)
endif()

if(NOT _formats)
    message(FATAL_ERROR "No plugin formats enabled!")
endif()

message(STATUS "Building Villain with formats: ${_formats}")

# ==============================================================================
# Villain Plugin
# ==============================================================================
juce_add_plugin(Villain
    COMPANY_NAME "Fanan"
    COMPANY_COPYRIGHT "Copyright (c) Fanan"
    PRODUCT_NAME "Villain"
    PLUGIN_MANUFACTURER_CODE FANa
    PLUGIN_CODE VLAN
    FORMATS ${_formats}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    MICROPHONE_PERMISSION_ENABLED FALSE
    CAMERA_PERMISSION_ENABLED FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
)

target_sources(Villain PRIVATE ${VILLAIN_SRC})

target_compile_features(Villain PRIVATE cxx_std_17)

target_compile_definitions(Villain
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(Villain
    PRIVATE
        VillainAssets
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
        juce::juce_recommended_lto_flags
)

# ==============================================================================
# CLAP support (when enabled)
# ==============================================================================
if(VILLAIN_ENABLE_CLAP AND TARGET clap_juce_extensions)
    target_link_libraries(Villain PRIVATE clap_juce_extensions)

    clap_juce_extensions_plugin(
        TARGET Villain
        CLAP_ID "com.fanan.villain"
        CLAP_FEATURES "audio-effect" "stereo" "mixing" "mastering"
        CLAP_PROCESS_EVENTS_RESOLUTION_SAMPLES 0
        CLAP_USE_JUCE_PARAMETER_RANGES DISCRETE
    )
endif()
