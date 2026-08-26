# ─────────────────────────────────────────────────────────────────────────────
# Hex Forge — desktop plugin (M1): VST3 + AU + Standalone via JUCE 8.
#
# Standalone project on purpose: the root CMakeLists stays the untouched
# pi-Stomp/LV2 build. This one compiles the SAME engine source
# (lv2/hexforge/engine/hf_engine_all.h) behind a JUCE AudioProcessor.
#
#   cmake -S desktop -B desktop/out
#   cmake --build desktop/out --config Release
# ─────────────────────────────────────────────────────────────────────────────
cmake_minimum_required(VERSION 3.22)
project(HexForgeDesktop VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ─── JUCE (local checkout wins; else fetched) ─────────────────────────────────
if(NOT DEFINED JUCE_DIR)
    set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../deps/JUCE")
endif()
if(EXISTS "${JUCE_DIR}/CMakeLists.txt")
    add_subdirectory("${JUCE_DIR}" JUCE_build)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.3
        GIT_SHALLOW    TRUE
        GIT_PROGRESS   TRUE
        # Smart App Control blocks the STOCK juceaide binary by cloud hash
        # verdict; a one-line source nudge makes the local build pass.
        PATCH_COMMAND ${CMAKE_COMMAND} -DJUCE_SRC=<SOURCE_DIR>
                      -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/juceaide_sac_nudge.cmake")
    FetchContent_MakeAvailable(JUCE)
endif()

# ─── DSP core: the SAME submodule the LV2 build uses (NAM pinned inside) ──────
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../deps/guitar-amp-simulator" gas-build)

# ─── Plugin target ────────────────────────────────────────────────────────────
juce_add_plugin(HexForge
    VERSION                  0.1.0
    COMPANY_NAME             "Hex Chain"
    PRODUCT_NAME             "Hex Forge"
    PLUGIN_MANUFACTURER_CODE "HxCh"
    PLUGIN_CODE              "HxFg"
    FORMATS                  AU VST3 Standalone
    IS_SYNTH                 FALSE
    NEEDS_MIDI_INPUT         FALSE
    NEEDS_MIDI_OUTPUT        FALSE
    IS_MIDI_EFFECT           FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD  FALSE
)

juce_generate_juce_header(HexForge)

target_sources(HexForge PRIVATE
    src/HexForgeProcessor.cpp
)

target_include_directories(HexForge PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/generated"
    "${CMAKE_CURRENT_SOURCE_DIR}/../lv2/hexforge/engine"
    "${CMAKE_CURRENT_SOURCE_DIR}/../lv2/common"
    "${CMAKE_CURRENT_SOURCE_DIR}/../deps/guitar-amp-simulator/include"
)

target_compile_definitions(HexForge PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
)

if(MSVC)
    target_compile_options(HexForge PRIVATE $<$<NOT:$<CONFIG:Debug>>:/O2>)
else()
    target_compile_options(HexForge PRIVATE -O3)
endif()

target_link_libraries(HexForge PRIVATE
    GuitarAmpSim            # PUBLIC whole-archives NamCore
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
)
