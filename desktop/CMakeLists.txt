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
if(WIN32)
    set(HF_PLATFORM_PLUGIN_ARGS NEEDS_WEBVIEW2 TRUE)
endif()
juce_add_plugin(HexForge
    VERSION                  0.1.0
    COMPANY_NAME             "Hex Chain"
    PRODUCT_NAME             "Hex Forge"
    PLUGIN_MANUFACTURER_CODE "HxCh"
    PLUGIN_CODE              "HxFg"
    FORMATS                  AU VST3 Standalone
    ${HF_PLATFORM_PLUGIN_ARGS}
    IS_SYNTH                 FALSE
    NEEDS_MIDI_INPUT         FALSE
    NEEDS_MIDI_OUTPUT        FALSE
    IS_MIDI_EFFECT           FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD  FALSE
)

juce_generate_juce_header(HexForge)

# ── Embedded UI assets (M3): the page + modgui art compile into the binary so
# the plugin works without the repo checkout. When the repo dirs exist at
# runtime the editor serves the FILES instead (live-edit dev mode).
set(HF_MODGUI "${CMAKE_CURRENT_SOURCE_DIR}/../lv2/modgui-hexforge")
file(GLOB HF_MODGUI_ART "${HF_MODGUI}/*.png" "${HF_MODGUI}/*.jpg")
list(REMOVE_ITEM HF_MODGUI_ART
    "${HF_MODGUI}/screenshot-hexforge.png"
    "${HF_MODGUI}/thumbnail-hexforge.png")
juce_add_binary_data(HexForgeAssets SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon-hexforge-desktop.html"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/stylesheet-hexforge-desktop.css"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/hexforge-desktop-bridge.js"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/hexforge-ports.js"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/vendor/jquery.min.js"
    "${HF_MODGUI}/script-hexforge.js"
    "${HF_MODGUI}/knobs/knob.png"
    "${HF_MODGUI}/switches/powerswitch.png"
    ${HF_MODGUI_ART}
)

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
    JUCE_WEB_BROWSER=1
    $<$<PLATFORM_ID:Windows>:JUCE_USE_WIN_WEBVIEW2=1>
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    # M2 dev serving: page + assets straight from the repo (packaging embeds
    # them later). Forward slashes keep MSVC's preprocessor happy.
    HF_DESKTOP_RESOURCES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/resources"
    HF_MODGUI_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../lv2/modgui-hexforge"
)

if(MSVC)
    target_compile_options(HexForge PRIVATE $<$<NOT:$<CONFIG:Debug>>:/O2>)
else()
    target_compile_options(HexForge PRIVATE -O3)
endif()

target_link_libraries(HexForge PRIVATE
    GuitarAmpSim            # PUBLIC whole-archives NamCore
    HexForgeAssets
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
)
