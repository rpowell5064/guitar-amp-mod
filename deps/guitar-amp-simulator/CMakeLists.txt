cmake_minimum_required(VERSION 3.16)
project(GuitarAmpSimulator VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ─── NeuralAmpModelerCore ─────────────────────────────────────────────────────
include(FetchContent)
FetchContent_Declare(NeuralAmpModelerCore
    GIT_REPOSITORY https://github.com/sdatkinson/NeuralAmpModelerCore.git
    # Pin to a known-good commit (v0.5.3+6) — upstream `main` later changed the
    # nam::DSP API (removed ResetAndPrewarm), which broke fresh CI builds. This is
    # the exact NAM core the verified on-device builds use. (Not a tag tip, so
    # GIT_SHALLOW must be FALSE to fetch an arbitrary SHA.)
    GIT_TAG        5edb0bae2a7456816a3ee3b8909d6c8a6fbd0ad9
    GIT_SHALLOW    FALSE
    GIT_PROGRESS   TRUE)
# Populate sources only — do NOT run NAMCore's CMakeLists (it builds tools we don't need).
FetchContent_GetProperties(NeuralAmpModelerCore)
if(NOT neuralampmodelercore_POPULATED)
    FetchContent_Populate(NeuralAmpModelerCore)
endif()

# NAM does not export a CMake library target — build one from its sources.
file(GLOB_RECURSE NAM_SRCS "${neuralampmodelercore_SOURCE_DIR}/NAM/*.cpp")
add_library(NamCore STATIC ${NAM_SRCS})
set_target_properties(NamCore PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED OFF)
target_include_directories(NamCore PUBLIC
    "${neuralampmodelercore_SOURCE_DIR}/NAM"
    "${neuralampmodelercore_SOURCE_DIR}/Dependencies"
    "${neuralampmodelercore_SOURCE_DIR}/Dependencies/eigen"
    "${neuralampmodelercore_SOURCE_DIR}/Dependencies/nlohmann"
)
target_compile_definitions(NamCore PUBLIC NAM_SAMPLE_FLOAT)
if(MSVC)
    target_compile_definitions(NamCore PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(NamCore PRIVATE /W0)
endif()

# ─── GuitarAmpSim library ─────────────────────────────────────────────────────
file(GLOB_RECURSE LIB_SOURCES "src/*.cpp")

add_library(GuitarAmpSim STATIC ${LIB_SOURCES})
target_include_directories(GuitarAmpSim PUBLIC include)
# WHOLE_ARCHIVE ensures static initializers in NAMCore run (they register
# architecture parsers in ConfigParserRegistry at program startup).
target_link_libraries(GuitarAmpSim PUBLIC $<LINK_LIBRARY:WHOLE_ARCHIVE,NamCore>)

if(MSVC)
    target_compile_options(GuitarAmpSim PRIVATE /W4 $<$<NOT:$<CONFIG:Debug>>:/O2>)
else()
    target_compile_options(GuitarAmpSim PRIVATE -Wall -Wextra -O3 -ffast-math -march=native)
endif()

add_executable(GuitarAmpExample example/main.cpp)
target_link_libraries(GuitarAmpExample PRIVATE GuitarAmpSim)

add_executable(TestNam example/test_nam.cpp)
target_link_libraries(TestNam PRIVATE GuitarAmpSim)
