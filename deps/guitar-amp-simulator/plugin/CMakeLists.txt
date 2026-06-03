cmake_minimum_required(VERSION 3.22)
project(GuitarAmpPlugin VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ─── JUCE ─────────────────────────────────────────────────────────────────────
if(NOT DEFINED JUCE_DIR)
    set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../JUCE")
endif()
if(EXISTS "${JUCE_DIR}/CMakeLists.txt")
    add_subdirectory("${JUCE_DIR}" JUCE_build)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.3
        GIT_SHALLOW    TRUE
        GIT_PROGRESS   TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# ─── NeuralAmpModelerCore ─────────────────────────────────────────────────────
include(FetchContent)
FetchContent_Declare(NeuralAmpModelerCore
    GIT_REPOSITORY https://github.com/sdatkinson/NeuralAmpModelerCore.git
    GIT_TAG        main
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE)
# Populate sources only — do NOT run NAMCore's CMakeLists (it builds tools we don't need).
FetchContent_GetProperties(NeuralAmpModelerCore)
if(NOT neuralampmodelercore_POPULATED)
    FetchContent_Populate(NeuralAmpModelerCore)
endif()

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

# ─── Plugin target ─────────────────────────────────────────────────────────────
juce_add_plugin(GuitarAmpPlugin
    VERSION                  1.0.0
    COMPANY_NAME             "AmpSim"
    PRODUCT_NAME             "Guitar Amp Sim"
    PLUGIN_MANUFACTURER_CODE "GCAS"
    PLUGIN_CODE              "GAMP"
    FORMATS                  AU VST3 Standalone
    IS_SYNTH                 FALSE
    NEEDS_MIDI_INPUT         FALSE
    NEEDS_MIDI_OUTPUT        FALSE
    IS_MIDI_EFFECT           FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD  FALSE
)

juce_generate_juce_header(GuitarAmpPlugin)

set(DSP_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../src")
set(DSP_INC "${CMAKE_CURRENT_SOURCE_DIR}/../include")

target_sources(GuitarAmpPlugin PRIVATE
    # DSP engine (from parent project)
    "${DSP_SRC}/ParameterManager.cpp"
    "${DSP_SRC}/NoiseGateBlock.cpp"
    "${DSP_SRC}/PitchBlock.cpp"
    "${DSP_SRC}/AmpBlock.cpp"
    "${DSP_SRC}/NamModel.cpp"
    "${DSP_SRC}/PowerAmpProcessor.cpp"
    "${DSP_SRC}/OlaConvolver.cpp"
    "${DSP_SRC}/CabinetBlock.cpp"
    "${DSP_SRC}/PlateReverbBlock.cpp"
    "${DSP_SRC}/OutputEQBlock.cpp"
    "${DSP_SRC}/SignalChain.cpp"
    "${DSP_SRC}/GuitarAmpProcessor.cpp"
    "${DSP_SRC}/OversamplingWrapper.cpp"
    "${DSP_SRC}/AmpModelFactory.cpp"
    "${DSP_SRC}/SunnModelT.cpp"
    "${DSP_SRC}/SunnModelTToneStack.cpp"
    "${DSP_SRC}/SunnPowerAmp6550.cpp"
    "${DSP_SRC}/SunnT_TriodeStage.cpp"
    "${DSP_SRC}/Rockerverb50.cpp"
    "${DSP_SRC}/TriodeComponent.cpp"
    "${DSP_SRC}/ToneStackComponent.cpp"
    "${DSP_SRC}/JCM800Model.cpp"
    "${DSP_SRC}/FenderDeluxeModel.cpp"
    "${DSP_SRC}/EVH5150Model.cpp"
    "${DSP_SRC}/CathodeFollower.cpp"
    "${DSP_SRC}/PhaseInverter.cpp"
    "${DSP_SRC}/PowerTubeStage.cpp"
    "${DSP_SRC}/PowerSupplySag.cpp"
    "${DSP_SRC}/NegativeFeedbackLoop.cpp"
    "${DSP_SRC}/PresenceResonanceNetwork.cpp"
    "${DSP_SRC}/OutputTransformerModel.cpp"
    "${DSP_SRC}/SpeakerImpedanceModel.cpp"
    "${DSP_SRC}/AmpFrequencyAnalyzer.cpp"
    # Overdrive block (unified model manager + individual models)
    "${DSP_SRC}/TubeScreamer808Block.cpp"
    "${DSP_SRC}/TubeScreamer808.cpp"
    "${DSP_SRC}/LifePedal.cpp"
    "${DSP_SRC}/NamOverdrive.cpp"
    "${DSP_SRC}/ProcoRAT.cpp"
    "${DSP_SRC}/EHXBigMuff.cpp"
    "${DSP_SRC}/OverdriveFactory.cpp"
    "${DSP_SRC}/OverdriveBlock.cpp"
    # Delay block (unified model manager + individual models)
    "${DSP_SRC}/DelayBlock.cpp"
    "${DSP_SRC}/DigitalDelay.cpp"
    "${DSP_SRC}/TapeDelay.cpp"
    "${DSP_SRC}/EchorecDelay.cpp"
    "${DSP_SRC}/DelayFactory.cpp"
    # Modulation block (CE-2 Chorus + Uni-Vibe + factory + block manager)
    "${DSP_SRC}/CE2Chorus.cpp"
    "${DSP_SRC}/UniVibeEffect.cpp"
    "${DSP_SRC}/ModulationFactory.cpp"
    "${DSP_SRC}/ModulationBlock.cpp"
    # Plugin layer
    PluginProcessor.cpp
    PluginEditor.cpp
    # UI components
    ui/AmpSimLookAndFeel.cpp
    ui/NoiseGateComponent.cpp
    ui/TubeScreamerComponent.cpp
    ui/AmpComponent.cpp
    ui/CabinetIRComponent.cpp
    ui/DelayComponent.cpp
    ui/ReverbComponent.cpp
    ui/OutputEQComponent.cpp
    ui/OutputComponent.cpp
    ui/ModulationComponent.cpp
    ui/PitchComponent.cpp
    ui/HorizontalChainComponent.cpp
    ui/PresetBrowserComponent.cpp
    ui/CompressorComponent.cpp
)

target_include_directories(GuitarAmpPlugin PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${DSP_INC}"
    "${CMAKE_CURRENT_SOURCE_DIR}/ui"
)

target_compile_definitions(GuitarAmpPlugin PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
)

if(MSVC)
    target_compile_options(GuitarAmpPlugin PRIVATE /W3 $<$<NOT:$<CONFIG:Debug>>:/O2>)
else()
    target_compile_options(GuitarAmpPlugin PRIVATE -Wall -Wextra -O2)
endif()

target_link_libraries(GuitarAmpPlugin PRIVATE
    $<LINK_LIBRARY:WHOLE_ARCHIVE,NamCore>
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)
