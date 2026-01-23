cmake_minimum_required(VERSION 3.20)

project(Villain VERSION 1.0.0 LANGUAGES CXX)

# ==============================================================================
# Global options (single CMakeLists supports both Offline and Online builds)
# ==============================================================================
option(VILLAIN_BUILD_OFFLINE "Build the Offline (no cloud) plugin variant" ON)
option(VILLAIN_BUILD_ONLINE  "Build the Online (cloud) plugin variant"     ON)

option(VILLAIN_ENABLE_VST3   "Enable VST3 format" ON)
option(VILLAIN_ENABLE_AU     "Enable AU format (macOS only)" ON)
option(VILLAIN_ENABLE_CLAP   "Enable CLAP format (desktop via clap-juce-extensions)" ON)

# Optional suffix appended to the plugin product name (e.g. ' Nightly', ' Beta')
set(VILLAIN_NAME_SUFFIX "" CACHE STRING "Optional suffix appended to the plugin product name")

# JUCE location: override from CLI in CI with -DJUCE_DIR=...
# Keeping your local default for convenience, but CI should pass JUCE_DIR explicitly.
set(JUCE_DIR "D:/Workspace/JUCE" CACHE PATH "Path to JUCE")

# CLAP extensions location: required only when VILLAIN_ENABLE_CLAP=ON.
# In CI you will likely pass -DCLAP_EXTENSIONS_DIR=libs/CLAP_Extensions
set(CLAP_EXTENSIONS_DIR "" CACHE PATH "Path to clap-juce-extensions root folder")

# ==============================================================================
# JUCE
# ==============================================================================
add_subdirectory("${JUCE_DIR}" JUCE)

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
# Assets (shared by all variants)
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
# Helper: add a Villain plugin variant (Offline/Online)
# ==============================================================================
function(add_villain_variant TARGET_NAME VARIANT_LABEL IS_ONLINE PLUGIN_CODE_4)
    # Build formats list from options and platform.
    set(_formats)

    if (VILLAIN_ENABLE_VST3)
        list(APPEND _formats VST3)
    endif()

    if (APPLE AND VILLAIN_ENABLE_AU)
        list(APPEND _formats AU)
    endif()

    # CLAP is supported on desktop via clap-juce-extensions; avoid iOS.
    if (VILLAIN_ENABLE_CLAP AND NOT IOS)
        list(APPEND _formats CLAP)
    endif()

    if (NOT _formats)
        message(FATAL_ERROR "No plugin formats enabled. Enable at least one of: VILLAIN_ENABLE_VST3 / VILLAIN_ENABLE_AU / VILLAIN_ENABLE_CLAP")
    endif()

    juce_add_plugin(${TARGET_NAME}
        COMPANY_NAME "Fanan"
        COMPANY_COPYRIGHT "Copyright (c) Fanan"
        PRODUCT_NAME "Villain${VILLAIN_NAME_SUFFIX} ${VARIANT_LABEL}"
        PLUGIN_MANUFACTURER_CODE FANa
        PLUGIN_CODE ${PLUGIN_CODE_4}
        FORMATS ${_formats}
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        MICROPHONE_PERMISSION_ENABLED FALSE
        CAMERA_PERMISSION_ENABLED FALSE
        COPY_PLUGIN_AFTER_BUILD TRUE
    )

    target_sources(${TARGET_NAME} PRIVATE ${VILLAIN_SRC})

    target_compile_features(${TARGET_NAME} PRIVATE cxx_std_17)

    target_compile_definitions(${TARGET_NAME}
        PRIVATE
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_VST3_CAN_REPLACE_VST2=0
            VILLAIN_ONLINE_BUILD=$<IF:${IS_ONLINE},1,0>
            VILLAIN_OFFLINE_BUILD=$<IF:${IS_ONLINE},0,1>
    )

    target_link_libraries(${TARGET_NAME}
        PRIVATE
            VillainAssets
            juce::juce_audio_utils
            juce::juce_audio_processors
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags
            juce::juce_recommended_lto_flags
    )

    # ------------------------------------------------------------
    # Optional CLAP via clap-juce-extensions (desktop only)
    # ------------------------------------------------------------
    if (VILLAIN_ENABLE_CLAP AND NOT IOS)
        if (CLAP_EXTENSIONS_DIR STREQUAL "")
            message(FATAL_ERROR
                "VILLAIN_ENABLE_CLAP=ON but CLAP_EXTENSIONS_DIR is empty.\n"
                "Pass -DCLAP_EXTENSIONS_DIR=<path to clap-juce-extensions> or disable CLAP."
            )
        endif()

        # clap-juce-extensions provides a CMakeLists at its root.
        # We add it once per build-tree; use a stable binary dir name to avoid collisions.
        if (NOT TARGET clap_juce_extensions)
            add_subdirectory("${CLAP_EXTENSIONS_DIR}" "${CMAKE_BINARY_DIR}/clap_juce_extensions_build")
        endif()

        target_link_libraries(${TARGET_NAME} PRIVATE clap_juce_extensions)

        # This generates the .clap target/bundle for the plugin.
        # The function is defined by clap-juce-extensions after add_subdirectory().
        clap_juce_extensions_plugin(
            TARGET ${TARGET_NAME}
            CLAP_ID "com.fanan.villain.${VARIANT_LABEL}"
            CLAP_FEATURES audio-effect
        )
    endif()
endfunction()

# ==============================================================================
# Variants
# ==============================================================================
if (VILLAIN_BUILD_OFFLINE)
    add_villain_variant(Villain_Offline "Offline" FALSE VLOF)
endif()

if (VILLAIN_BUILD_ONLINE)
    add_villain_variant(Villain_Online "Online" TRUE VLON)
endif()

if (NOT VILLAIN_BUILD_OFFLINE AND NOT VILLAIN_BUILD_ONLINE)
    message(FATAL_ERROR "Both VILLAIN_BUILD_OFFLINE and VILLAIN_BUILD_ONLINE are OFF. Enable at least one variant.")
endif()
