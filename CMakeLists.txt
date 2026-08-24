cmake_minimum_required(VERSION 3.22)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

project(FemPlate VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS deployment version")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Target architectures")
endif()

# JUCE is expected as a sibling of this repo; FxmeTools lives in-tree as a git
# submodule under lib/ (registered directly: FemPlate does not need the WDL
# sources, so fxmetools_attach is not used).
#
# FxmeTools is split in two, and the FEM code FemPlate is built on — FemMesh and
# PlateModes — is entirely in the JUCE-free half under lib/FxmeTools/core. That
# directory is a second include root for the same <FxmeTools/...> spelling, so
# no include changes, but the target has to be added or nothing resolves.
add_subdirectory(../JUCE JUCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/lib/FxmeTools/core FxmeCore)
juce_add_module(${CMAKE_CURRENT_SOURCE_DIR}/lib/FxmeTools/FxmeTools)

# The module's own sources include core headers, and juce_add_module creates an
# INTERFACE target whose sources compile into the consumer, so core travels
# with it.
target_link_libraries(FxmeTools INTERFACE FxmeCore)

juce_add_plugin(FemPlate
    COMPANY_NAME                "FX-Mechanics"
    COMPANY_WEBSITE             "www.fx-mechanics.com"
    PLUGIN_MANUFACTURER_CODE    FXME
    PLUGIN_CODE                 FMPL
    FORMATS                     AU VST3 Standalone
    COPY_PLUGIN_AFTER_BUILD     TRUE
    PRODUCT_NAME                "FemPlate"
    DESCRIPTION                 "FX-Mechanics finite-element plate physical model"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES             Fx Instrument
    # An audio effect that listens to notes: kAudioUnitType_Effect ('aufx')
    # has no MIDI input at all in the AU spec, so the AU would silently never
    # receive a note whatever NEEDS_MIDI_INPUT says. 'aumf' is the type for
    # this, and Logic lists it under MIDI-controlled Effects rather than
    # Audio FX.
    AU_MAIN_TYPE                kAudioUnitType_MusicEffect
    BUNDLE_ID                   "com.FXMechanics.FemPlate"
)

# Source files are discovered recursively so adding a new module/component
# does not require editing this file. Re-run CMake configure after adding files.
file(GLOB_RECURSE FEMPLATE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/*.h
)

target_sources(FemPlate PRIVATE ${FEMPLATE_SOURCES})

# Make every subdirectory under Source/ an include root so headers can be
# included by name regardless of where a module lives.
file(GLOB FEMPLATE_INCLUDE_DIRS LIST_DIRECTORIES true
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/*
)
list(FILTER FEMPLATE_INCLUDE_DIRS EXCLUDE REGEX "\\.(c|cc|cpp|cxx|h|hpp|hxx|mm)$")
target_include_directories(FemPlate PRIVATE ${FEMPLATE_INCLUDE_DIRS})

# Image assets (logo, etc.)
file(GLOB FEMPLATE_ASSETS ${CMAKE_CURRENT_SOURCE_DIR}/Source/Assets/*.png
                          ${CMAKE_CURRENT_SOURCE_DIR}/Source/Assets/*.jpg)
if(FEMPLATE_ASSETS)
    juce_add_binary_data(FemPlateBinaryData SOURCES ${FEMPLATE_ASSETS})
    set_target_properties(FemPlateBinaryData PROPERTIES
        INTERPROCEDURAL_OPTIMIZATION OFF)
    target_link_libraries(FemPlate PRIVATE FemPlateBinaryData)
endif()

juce_generate_juce_header(FemPlate)

target_compile_definitions(FemPlate
    PUBLIC
        JUCE_STRICT_REFCOUNTEDPOINTER=1
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_MODAL_LOOPS_PERMITTED=1
)

target_link_libraries(FemPlate
    PRIVATE
        FxmeTools
        FxmeCore
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

# Console validation of the JUCE-free FEM core (mesh generation, Morley plate
# eigensolver vs analytic references). Build with -DFEMPLATE_BUILD_TESTS=ON,
# run with ctest (or ./FemTests directly).
option(FEMPLATE_BUILD_TESTS "Build FemPlate unit tests" OFF)
if(FEMPLATE_BUILD_TESTS)
    enable_testing()
    # FemMesh.cpp and PlateModes.cpp used to be listed here by path. They now
    # live in core and FxmeCore compiles them, so this links that instead of
    # naming the files — which also means the list cannot go stale again the
    # next time something moves. FemCore carries its own include root, and this
    # target needs no other: FemTests.cpp includes only core headers, so it now
    # touches the JUCE module not at all, which is what "JUCE-free FEM core"
    # above always meant.
    add_executable(FemTests Tests/FemTests.cpp)
    target_link_libraries(FemTests PRIVATE FxmeCore)
    target_compile_features(FemTests PRIVATE cxx_std_17)
    add_test(NAME FemTests COMMAND FemTests)

    # Renders the real modal synthesis offline to measure the cascade drive
    # (see Tests/CascadeMeasure.cpp). PlateSynth.cpp is compiled straight into
    # it: its whole JUCE surface is a few numeric helpers, supplied by the stub
    # header, so this needs neither JUCE nor a host. Should the stub ever stop
    # sufficing, the DSP has acquired a real JUCE dependency worth knowing
    # about.
    add_executable(CascadeMeasure Tests/CascadeMeasure.cpp Source/Dsp/PlateSynth.cpp)
    target_include_directories(CascadeMeasure PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Tests/juce_stub
        ${CMAKE_CURRENT_SOURCE_DIR}/Source
        ${CMAKE_CURRENT_SOURCE_DIR}/Source/Dsp)
    target_link_libraries(CascadeMeasure PRIVATE FxmeCore)
    target_compile_features(CascadeMeasure PRIVATE cxx_std_17)
    add_test(NAME CascadeMeasure COMMAND CascadeMeasure)
endif()
