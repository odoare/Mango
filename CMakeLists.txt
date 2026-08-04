cmake_minimum_required(VERSION 3.22)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# These MUST come before project(): that is where CMake configures the Apple
# toolchain and reads them. Set afterwards, CMAKE_OSX_DEPLOYMENT_TARGET is
# ignored outright and the architectures are unreliable — which is how v0.1.0
# shipped an arm64-only "universal" build from an Apple Silicon runner.
#
# 10.13 is JUCE 8's floor. It has to be well below 11.0 or the plugin refuses
# to load on Catalina (10.15); the arm64 slice of a universal build is clamped
# up to 11.0 by the toolchain on its own, so this costs nothing.
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS deployment version")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Target architectures")
endif()

project(Mango VERSION 0.1.3 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE is expected as a sibling of this repo; FxmeTools lives in-tree as a git
# submodule under lib/. The module is registered directly rather than through
# fxmetools_attach: of the WDL sources (FxmeTools' own submodule) Mango only
# needs fft.c, for <FxmeTools/dsp/SpectralFreeze.h> (the Freeze effect).
add_subdirectory(../JUCE JUCE)
juce_add_module(${CMAKE_CURRENT_SOURCE_DIR}/lib/FxmeTools/FxmeTools)
set(FXMETOOLS_WDL_FFT ${CMAKE_CURRENT_SOURCE_DIR}/lib/FxmeTools/WDL/WDL/fft.c)

juce_add_plugin(Mango
    COMPANY_NAME                "FX-Mechanics"
    COMPANY_WEBSITE             "www.fx-mechanics.com"
    PLUGIN_MANUFACTURER_CODE    FXME
    PLUGIN_CODE                 MNGO
    FORMATS                     AU VST3 Standalone
    COPY_PLUGIN_AFTER_BUILD     TRUE
    PRODUCT_NAME                "Mango"
    DESCRIPTION                 "FX-Mechanics modular sound glitcher / mangler"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES             Fx Distortion
    # MusicEffect, not Effect: in the AU spec kAudioUnitType_Effect ('aufx')
    # has no MIDI input at all, so an AU declared that way can never receive
    # notes however the host is routed — which killed mididur/midifreq in the
    # AU build. An audio effect that accepts MIDI must be 'aumf'.
    # (Logic lists 'aumf' plugins under MIDI-controlled Effects, not Audio FX.)
    AU_MAIN_TYPE                kAudioUnitType_MusicEffect
    BUNDLE_ID                   "com.FXMechanics.Mango"
)

# Source files are discovered recursively so adding a new module/component
# does not require editing this file. Re-run CMake configure after adding files.
file(GLOB_RECURSE MANGO_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/*.h
)

target_sources(Mango PRIVATE ${MANGO_SOURCES} ${FXMETOOLS_WDL_FFT})

# Make every subdirectory under Source/ an include root so headers can be
# included by name regardless of where a module lives.
file(GLOB MANGO_INCLUDE_DIRS LIST_DIRECTORIES true
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/*
)
list(FILTER MANGO_INCLUDE_DIRS EXCLUDE REGEX "\\.(c|cc|cpp|cxx|h|hpp|hxx|mm)$")
target_include_directories(Mango PRIVATE ${MANGO_INCLUDE_DIRS})

# Image assets (logo, artwork, splash) + factory preset XML. The presets are
# APVTS-state XML saved by the plugin; PresetManager keeps every embedded
# "*_xml" resource whose root tag matches the APVTS state type ("Parameters")
# and ignores the rest, so images and presets can share one binary-data blob.
file(GLOB MANGO_ASSETS ${CMAKE_CURRENT_SOURCE_DIR}/Source/Assets/*.png
                       ${CMAKE_CURRENT_SOURCE_DIR}/Source/Assets/*.jpg
                       ${CMAKE_CURRENT_SOURCE_DIR}/Source/Assets/*.xml)
if(MANGO_ASSETS)
    juce_add_binary_data(MangoBinaryData SOURCES ${MANGO_ASSETS})
    set_target_properties(MangoBinaryData PROPERTIES
        INTERPROCEDURAL_OPTIMIZATION OFF)
    target_link_libraries(Mango PRIVATE MangoBinaryData)
endif()

juce_generate_juce_header(Mango)

target_compile_definitions(Mango
    PUBLIC
        JUCE_STRICT_REFCOUNTEDPOINTER=1
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_MODAL_LOOPS_PERMITTED=1
)

target_link_libraries(Mango
    PRIVATE
        FxmeTools
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_plugin_client
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_core
        juce::juce_data_structures
        juce::juce_dsp
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Console unit tests for the JUCE-free pieces (FxmeTools DSP additions and the
# Mango override parser). Build with -DMANGO_BUILD_TESTS=ON, run with ctest.
option(MANGO_BUILD_TESTS "Build Mango unit tests" OFF)
if(MANGO_BUILD_TESTS)
    enable_testing()
    add_executable(MangoTests Tests/UnitTests.cpp ${FXMETOOLS_WDL_FFT})
    target_include_directories(MangoTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/FxmeTools
        ${CMAKE_CURRENT_SOURCE_DIR}/Source)
    target_compile_features(MangoTests PRIVATE cxx_std_17)
    add_test(NAME MangoTests COMMAND MangoTests)

    # Offline render checks of the full processor (gater timing, seed
    # determinism, quantizer). Built as an independent console app compiling
    # the processor sources directly (reusing Mango's generated JuceHeader).
    juce_add_console_app(MangoRenderTest PRODUCT_NAME "MangoRenderTest")
    target_sources(MangoRenderTest PRIVATE
        Tests/RenderTest.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/Dsp/MangoEngine.cpp
        ${FXMETOOLS_WDL_FFT})
    target_include_directories(MangoRenderTest PRIVATE
        ${CMAKE_BINARY_DIR}/Mango_artefacts/JuceLibraryCode
        ${CMAKE_CURRENT_SOURCE_DIR}/Source)
    target_compile_definitions(MangoRenderTest PRIVATE
        JucePlugin_Name="Mango"
        JucePlugin_VersionString="${PROJECT_VERSION}"
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(MangoRenderTest PRIVATE
        FxmeTools
        MangoBinaryData
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_core
        juce::juce_data_structures
        juce::juce_dsp
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
        juce::juce_recommended_config_flags)
    add_dependencies(MangoRenderTest Mango)
    add_test(NAME MangoRenderTest COMMAND MangoRenderTest)
endif()
