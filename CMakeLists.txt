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
add_subdirectory(../JUCE JUCE)
juce_add_module(${CMAKE_CURRENT_SOURCE_DIR}/lib/FxmeTools/FxmeTools)

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
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES             Fx Instrument
    AU_MAIN_TYPE                kAudioUnitType_Effect
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
    add_executable(FemTests
        Tests/FemTests.cpp
        lib/FxmeTools/FxmeTools/acoustics/FemMesh.cpp
        lib/FxmeTools/FxmeTools/acoustics/PlateModes.cpp)
    target_include_directories(FemTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/lib/FxmeTools)
    target_compile_features(FemTests PRIVATE cxx_std_17)
    add_test(NAME FemTests COMMAND FemTests)
endif()
