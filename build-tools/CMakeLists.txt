cmake_minimum_required(VERSION 3.20)
project(namcmp CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Host-only scratch build for the nam_compare analysis tool. Reuses the DSP
# submodule; the real bundle build lives in the parent CMakeLists.
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../deps/guitar-amp-simulator" gas-build)
add_executable(nam_compare "${CMAKE_CURRENT_SOURCE_DIR}/../tools/nam_compare.cpp")
target_link_libraries(nam_compare PRIVATE GuitarAmpSim)
add_executable(fuzz_probe "${CMAKE_CURRENT_SOURCE_DIR}/../tools/fuzz_probe.cpp")
target_link_libraries(fuzz_probe PRIVATE GuitarAmpSim)
add_executable(seraph_test "${CMAKE_CURRENT_SOURCE_DIR}/../tools/seraph_test.cpp")
target_link_libraries(seraph_test PRIVATE GuitarAmpSim)
add_executable(amp_alias "${CMAKE_CURRENT_SOURCE_DIR}/../tools/amp_alias.cpp")
target_link_libraries(amp_alias PRIVATE GuitarAmpSim)
add_executable(amp_knobcheck "${CMAKE_CURRENT_SOURCE_DIR}/../tools/amp_knobcheck.cpp")
target_link_libraries(amp_knobcheck PRIVATE GuitarAmpSim)
add_executable(amp_noise "${CMAKE_CURRENT_SOURCE_DIR}/../tools/amp_noise.cpp")
target_link_libraries(amp_noise PRIVATE GuitarAmpSim)
add_executable(evh_hiss "${CMAKE_CURRENT_SOURCE_DIR}/../tools/evh_hiss.cpp")
target_link_libraries(evh_hiss PRIVATE GuitarAmpSim)
add_executable(fidelity_check "${CMAKE_CURRENT_SOURCE_DIR}/../tools/fidelity_check.cpp")
target_link_libraries(fidelity_check PRIVATE GuitarAmpSim)
add_executable(pa_flux_probe "${CMAKE_CURRENT_SOURCE_DIR}/../tools/pa_flux_probe.cpp")
target_link_libraries(pa_flux_probe PRIVATE GuitarAmpSim)
add_executable(vox_ts_check "${CMAKE_CURRENT_SOURCE_DIR}/../tools/vox_ts_check.cpp")
target_link_libraries(vox_ts_check PRIVATE GuitarAmpSim)
add_executable(conv_check "${CMAKE_CURRENT_SOURCE_DIR}/../tools/conv_check.cpp")
target_link_libraries(conv_check PRIVATE GuitarAmpSim)
add_executable(cab_voice_check "${CMAKE_CURRENT_SOURCE_DIR}/../tools/cab_voice_check.cpp")
target_link_libraries(cab_voice_check PRIVATE GuitarAmpSim)
# Preset-blob migration round-trip (header-only vs the generated port enum).
add_executable(hexforge_migrate_test "${CMAKE_CURRENT_SOURCE_DIR}/hexforge_migrate_test.cpp")
target_include_directories(hexforge_migrate_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../lv2/hexforge")

add_executable(pa_node_dump "${CMAKE_CURRENT_SOURCE_DIR}/../tools/pa_node_dump.cpp")
target_link_libraries(pa_node_dump PRIVATE GuitarAmpSim)

add_executable(vox_idle_probe "${CMAKE_CURRENT_SOURCE_DIR}/../tools/vox_idle_probe.cpp")
target_link_libraries(vox_idle_probe PRIVATE GuitarAmpSim)

# Auto-calibration measurement check (header-only DSP: synth reference-rig +
# real-capture modes; asserts the wizard reproduces the hand-tuned rig).
add_executable(cal_check "${CMAKE_CURRENT_SOURCE_DIR}/../tools/cal_check.cpp")
target_include_directories(cal_check PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../lv2/common"
    "${CMAKE_CURRENT_SOURCE_DIR}/../deps/guitar-amp-simulator/include")

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

# Compile-time proof that the engine umbrella needs no LV2 headers (the JUCE
# desktop processor's situation). Note: NO LV2 include dir on this target.
add_executable(hexforge_engine_standalone
    "${CMAKE_CURRENT_SOURCE_DIR}/hexforge_engine_standalone.cpp")
target_include_directories(hexforge_engine_standalone PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../lv2/hexforge/engine"
    "${CMAKE_CURRENT_SOURCE_DIR}/../lv2/common")
target_link_libraries(hexforge_engine_standalone PRIVATE GuitarAmpSim)
