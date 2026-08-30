cmake_minimum_required(VERSION 3.22)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# Before project(), and that is the whole point: project() latches the
# toolchain and the architecture list, so an architecture set after it is read
# too late. The build still succeeds and still calls itself universal, having
# quietly produced a single-architecture binary — which is why CI passes these
# as -D on the configure line as well, where the cache has them before any line
# of this file runs, and then verifies both slices are really present.
#
# 10.13 rather than 11.0: 11.0 silently excludes every Intel Mac still on
# Catalina, which is a large part of who would want a free plugin.
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS deployment version")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Target architectures")
endif()

project(ModalDish VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE is expected as a sibling of this repo; FxmeTools lives in-tree as a git
# submodule under lib/ (registered directly: ModalDish does not need the WDL
# sources, so fxmetools_attach is not used).
#
# FxmeTools is split in two, and the FEM code ModalDish is built on (FemMesh and
# PlateModes) is entirely in the JUCE-free half under lib/FxmeTools/core. That
# directory is a second include root for the same <FxmeTools/...> spelling, so
# no include changes, but the target has to be added or nothing resolves.
add_subdirectory(../JUCE JUCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/lib/FxmeTools/core FxmeCore)
juce_add_module(${CMAKE_CURRENT_SOURCE_DIR}/lib/FxmeTools/FxmeTools)

# The module's own sources include core headers, and juce_add_module creates an
# INTERFACE target whose sources compile into the consumer, so core travels
# with it.
target_link_libraries(FxmeTools INTERFACE FxmeCore)

juce_add_plugin(ModalDish
    COMPANY_NAME                "FX-Mechanics"
    COMPANY_WEBSITE             "www.fx-mechanics.com"
    PLUGIN_MANUFACTURER_CODE    FXME
    PLUGIN_CODE                 MDSH
    FORMATS                     AU VST3 Standalone
    COPY_PLUGIN_AFTER_BUILD     TRUE
    PRODUCT_NAME                "ModalDish"
    DESCRIPTION                 "FX-Mechanics finite-element plate physical model"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES             Fx
    AU_MAIN_TYPE                kAudioUnitType_MusicEffect
    BUNDLE_ID                   "com.FXMechanics.ModalDish"
)

# Source files are discovered recursively so adding a new module/component
# does not require editing this file. Re-run CMake configure after adding files.
file(GLOB_RECURSE MODALDISH_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/*.h
)

target_sources(ModalDish PRIVATE ${MODALDISH_SOURCES})

# Make every subdirectory under Source/ an include root so headers can be
# included by name regardless of where a module lives.
file(GLOB MODALDISH_INCLUDE_DIRS LIST_DIRECTORIES true
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/*
)
list(FILTER MODALDISH_INCLUDE_DIRS EXCLUDE REGEX "\\.(c|cc|cpp|cxx|h|hpp|hxx|mm)$")
target_include_directories(ModalDish PRIVATE ${MODALDISH_INCLUDE_DIRS})

# Image assets (logo, etc.)
# Images, plus the factory presets: PresetManager reads every embedded
# "*_xml" resource whose root tag matches the APVTS state, so dropping an
# exported preset into Source/Presets/ is all it takes to ship it.
# CONFIGURE_DEPENDS as the source glob has: without it, renaming or removing
# an asset leaves the build tree pointing at a file that is no longer there,
# and the failure names the old path rather than the stale glob.
file(GLOB MODALDISH_ASSETS CONFIGURE_DEPENDS
                          ${CMAKE_CURRENT_SOURCE_DIR}/Source/Assets/*.png
                          ${CMAKE_CURRENT_SOURCE_DIR}/Source/Assets/*.jpg
                          ${CMAKE_CURRENT_SOURCE_DIR}/Source/Presets/*.xml)
if(MODALDISH_ASSETS)
    juce_add_binary_data(ModalDishBinaryData SOURCES ${MODALDISH_ASSETS})
    set_target_properties(ModalDishBinaryData PROPERTIES
        INTERPROCEDURAL_OPTIMIZATION OFF)
    target_link_libraries(ModalDish PRIVATE ModalDishBinaryData)
endif()

juce_generate_juce_header(ModalDish)

target_compile_definitions(ModalDish
    PUBLIC
        JUCE_STRICT_REFCOUNTEDPOINTER=1
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_MODAL_LOOPS_PERMITTED=1
)

target_link_libraries(ModalDish
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
# eigensolver vs analytic references). Build with -DMODALDISH_BUILD_TESTS=ON,
# run with ctest (or ./FemTests directly).
option(MODALDISH_BUILD_TESTS "Build ModalDish unit tests" OFF)
if(MODALDISH_BUILD_TESTS)
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
    # The plate's input/output topology: pickups, sources, sends, note
    # dispatch. Same offline-render trick as CascadeMeasure below.
    add_executable(IoTests Tests/IoTests.cpp Source/Dsp/PlateSynth.cpp)
    target_include_directories(IoTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Tests/juce_stub
        ${CMAKE_CURRENT_SOURCE_DIR}/Source
        ${CMAKE_CURRENT_SOURCE_DIR}/Source/Dsp)
    target_link_libraries(IoTests PRIVATE FxmeCore)
    target_compile_features(IoTests PRIVATE cxx_std_17)
    add_test(NAME IoTests COMMAND IoTests)

    # The JSON geometry reader. The odd one out: it needs juce_core, because
    # what it tests is a JSON parser. Only juce_core though — no host, no
    # plugin client, no GUI — so it stays a console app and builds in seconds.
    juce_add_console_app(ShapeFileTests PRODUCT_NAME "ShapeFileTests")
    target_sources(ShapeFileTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Tests/ShapeFileTests.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Source/ShapeFile.cpp)
    juce_generate_juce_header(ShapeFileTests)
    target_include_directories(ShapeFileTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Source)
    # The plugin target's definitions do not reach here, and juce_core will
    # pull in libcurl at link time without this.
    target_compile_definitions(ShapeFileTests PRIVATE
        JUCE_USE_CURL=0
        JUCE_WEB_BROWSER=0)
    target_link_libraries(ShapeFileTests PRIVATE
        FxmeCore
        juce::juce_core
        juce::juce_recommended_config_flags)
    target_compile_features(ShapeFileTests PRIVATE cxx_std_17)
    add_test(NAME ShapeFileTests
             COMMAND ShapeFileTests ${CMAKE_CURRENT_SOURCE_DIR}/Shapes)

    add_executable(CascadeMeasure Tests/CascadeMeasure.cpp Source/Dsp/PlateSynth.cpp)
    target_include_directories(CascadeMeasure PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Tests/juce_stub
        ${CMAKE_CURRENT_SOURCE_DIR}/Source
        ${CMAKE_CURRENT_SOURCE_DIR}/Source/Dsp)
    target_link_libraries(CascadeMeasure PRIVATE FxmeCore)
    target_compile_features(CascadeMeasure PRIVATE cxx_std_17)
    add_test(NAME CascadeMeasure COMMAND CascadeMeasure)
endif()
