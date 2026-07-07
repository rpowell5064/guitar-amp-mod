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
