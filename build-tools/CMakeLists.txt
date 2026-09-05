cmake_minimum_required(VERSION 3.20)
project(namcmp CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Host-only scratch build for the nam_compare analysis tool. Reuses the DSP
# submodule; the real bundle build lives in the parent CMakeLists.
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../deps/guitar-amp-simulator" gas-build)
# Cab integrity gates (promoted from ad-hoc g++ for the bass-cab batch):
# Preset-blob migration round-trip (header-only vs the generated port enum).
add_executable(hexforge_migrate_test "${CMAKE_CURRENT_SOURCE_DIR}/hexforge_migrate_test.cpp")
target_include_directories(hexforge_migrate_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../lv2/hexforge")

# Auto-calibration measurement check (header-only DSP: synth reference-rig +
# real-capture modes; asserts the wizard reproduces the hand-tuned rig).

# ── Golden-render fingerprint harness (engine-extraction gate, M0) ────────────
# Statically links the WHOLE plugin TU (no dlopen) so it runs on Windows too.
# LV2 headers: system pkg-config where available, else fetched (header-only).
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(LV2 QUIET lv2)
endif()
if(LV2_FOUND)
    set(HF_LV2_INCLUDE ${LV2_INCLUDE_DIRS})
else()
    include(FetchContent)
    FetchContent_Declare(lv2headers
        URL https://github.com/lv2/lv2/archive/refs/tags/v1.18.10.tar.gz)
    FetchContent_MakeAvailable(lv2headers)
    set(HF_LV2_INCLUDE "${lv2headers_SOURCE_DIR}/include")
endif()
add_executable(hexforge_golden
    "${CMAKE_CURRENT_SOURCE_DIR}/hexforge_golden.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../lv2/hexforge/hexforge_plugin.cpp")
target_include_directories(hexforge_golden PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../lv2/hexforge"
    "${CMAKE_CURRENT_SOURCE_DIR}/../lv2/common"
    ${HF_LV2_INCLUDE})
target_link_libraries(hexforge_golden PRIVATE GuitarAmpSim)

# Desktop sample-rate converter verification (M3): SNR, image rejection,
# streaming equivalence across ragged block sizes.
add_executable(hf_resampler_test "${CMAKE_CURRENT_SOURCE_DIR}/hf_resampler_test.cpp")

# Anagram compile gates (M4 pilots + M5 full suite): every ported TU must
# build in BOTH flag states (default pi-Stomp AND -DHEXCHAIN_ANAGRAM=1) —
# object libs only, no linking (each TU exports lv2_descriptor).
foreach(mode default anagram)
    add_library(anagram_check_${mode} OBJECT
        "${CMAKE_CURRENT_SOURCE_DIR}/../lv2/drive/drive_plugin.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../lv2/fuzz/fuzz_plugin.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../lv2/amp/amp_plugin.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../lv2/cab/cab_plugin.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../lv2/comp/comp_plugin.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../lv2/gate/gate_plugin.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../lv2/delay/delay_plugin.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../lv2/modfx/modfx_plugin.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../lv2/nail/nail_plugin.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../lv2/octave/octave_plugin.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../lv2/reverb/reverb_plugin.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../lv2/wah/wah_plugin.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../lv2/utility/utility_plugin.cpp")
    target_include_directories(anagram_check_${mode} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../lv2/common"
        ${HF_LV2_INCLUDE})
    target_link_libraries(anagram_check_${mode} PRIVATE GuitarAmpSim)
endforeach()
target_compile_definitions(anagram_check_anagram PRIVATE HEXCHAIN_ANAGRAM=1)

# Compile-time proof that the engine umbrella needs no LV2 headers (the JUCE
# desktop processor's situation). Note: NO LV2 include dir on this target.
add_executable(hexforge_engine_standalone
    "${CMAKE_CURRENT_SOURCE_DIR}/hexforge_engine_standalone.cpp")
target_include_directories(hexforge_engine_standalone PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../lv2/hexforge/engine"
    "${CMAKE_CURRENT_SOURCE_DIR}/../lv2/common")
target_link_libraries(hexforge_engine_standalone PRIVATE GuitarAmpSim)
