cmake_minimum_required(VERSION 3.20)
project(GuitarAmpMOD CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The DSP engine builds static libs (GuitarAmpSim, NamCore) that get linked into
# our shared LV2 .so plugins. On aarch64 that requires position-independent code,
# so force PIC tree-wide BEFORE add_subdirectory (the submodule doesn't set it).
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Each plugin .so statically embeds DSP code. Loading several into one host
# process (mod-host) can interpose duplicate global symbols between plugins and
# corrupt shared state → crash. Hidden visibility helps but is NOT sufficient on
# its own (strong globals from NamCore/Eigen still leaked and crashed the host);
# the authoritative guard is the per-plugin version script (lv2/export.map) added
# in add_lv2_plugin, plus NOT linking NAM into plugins that don't use it (below).
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

# ── Architecture tuning ───────────────────────────────────────────────────────
# The submodule's CMakeLists compiles with -march=native. GCC honours the LAST
# -march, so appending GUITARAMP_ARCH_FLAGS below overrides it.
#   GUITARAMP_PORTABLE=ON  → generic -march=armv8-a: one aarch64 binary that runs on
#                            any Pi 4/5-class pi-Stomp. Use this for redistribution.
#   OFF (default)          → Pi 5 (Cortex-A76 + dot-product) when cross-compiling, or
#                            the host's -march=native for an on-device/personal build.
option(GUITARAMP_PORTABLE "Generic baseline build for redistribution" OFF)
# Explicit arch profile, overriding auto-detection. REQUIRED when CMake can't infer
# the real target ISA — e.g. an armv7 (arm32v7) container on an aarch64 host, where
# CMAKE_SYSTEM_PROCESSOR reports the host's "aarch64" even though the compiler is
# 32-bit armhf. Values: armv7 | armv8 | x86 | native | auto (default).
set(GUITARAMP_ARCH "auto" CACHE STRING "Arch flag profile: armv7|armv8|x86|native|auto")
# Arch flags are appended AFTER the submodule's -march=native so they win. They
# must match the target ISA — a build for armv7 must NOT inherit aarch64 or
# -march=native flags, or cc1plus errors out ("lacks an FPU").
if(GUITARAMP_ARCH STREQUAL "armv7")
    set(GUITARAMP_ARCH_FLAGS -march=armv7-a -mfpu=neon-vfpv4)            # patchbox-os-arm32
elseif(GUITARAMP_ARCH STREQUAL "armv8")
    set(GUITARAMP_ARCH_FLAGS -march=armv8-a)
elseif(GUITARAMP_ARCH STREQUAL "x86")
    set(GUITARAMP_ARCH_FLAGS -march=x86-64-v2 -mtune=generic)
elseif(GUITARAMP_ARCH STREQUAL "native")
    set(GUITARAMP_ARCH_FLAGS)
elseif(GUITARAMP_PORTABLE)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
        set(GUITARAMP_ARCH_FLAGS -march=armv8-a)                       # any Pi 4/5 (rpi-aarch64)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^arm")
        set(GUITARAMP_ARCH_FLAGS -march=armv7-a -mfpu=neon-vfpv4)  # patchbox-os-arm32 (Pi 2/3/4; gnueabihf = hard-float by default)
    else()
        set(GUITARAMP_ARCH_FLAGS -march=x86-64-v2 -mtune=generic)      # portable linux-amd64
    endif()
elseif(CMAKE_CROSSCOMPILING)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^arm" AND NOT CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
        set(GUITARAMP_ARCH_FLAGS -march=armv7-a -mfpu=neon-vfpv4)  # armhf cross (gnueabihf = hard-float)
    else()
        set(GUITARAMP_ARCH_FLAGS -march=armv8.2-a+dotprod -mtune=cortex-a76)  # Pi 5 cross
    endif()
else()
    set(GUITARAMP_ARCH_FLAGS)   # native on-device/personal build: keep -march=native
endif()

# ── NAM availability ──────────────────────────────────────────────────────────
# NAM (Neural Amp Modeler) is by far the heaviest DSP and depends on NamCore +
# Eigen. On weak / 32-bit targets (patchbox-os-arm32 → MODEP on Pi 3/4) it would
# never run in realtime, so we compile it out entirely:
#   GUITARAMP_DISABLE_NAM=ON →
#     • the amp/drive/cab/hexforge plugins get -DGUITARAMP_NO_NAM and include the
#       header-only NamModelStub.h instead of NamModel.h (no nam:: symbols), and
#     • NamCore is NOT linked into them. The algorithmic models stay intact.
# The "Neural (NAM)" model option is stripped from the TTL at packaging time, so
# the UI never offers it on these targets.
option(GUITARAMP_DISABLE_NAM "Compile out NAM/Eigen (weak/32-bit targets)" OFF)

# ── DSP engine (submodule) ────────────────────────────────────────────────────
# Provides: GuitarAmpSim (static, all DSP) + NamCore (static, NAM inference)
add_subdirectory(deps/guitar-amp-simulator)

if(GUITARAMP_ARCH_FLAGS)
    foreach(_T GuitarAmpSim NamCore)
        target_compile_options(${_T} PRIVATE ${GUITARAMP_ARCH_FLAGS})
    endforeach()
endif()

# ── NamCore optimization (the NAM neural inference — by far the heaviest DSP) ──
# The submodule applies -O3 -ffast-math -march=native to GuitarAmpSim ONLY; NamCore
# inherits just -O3 from the Release build type. Without -ffast-math (FMA
# contraction, Eigen fast paths) and CPU tuning, the WaveNet/LSTM inference runs
# well below the Pi's capability and stutters. Give NamCore the same flags.
# (-march=native is right for the on-device native build; cross/portable builds
#  already received GUITARAMP_ARCH_FLAGS in the loop above.)
if(NOT MSVC)
    target_compile_options(NamCore PRIVATE -ffast-math)
    if(NOT GUITARAMP_ARCH_FLAGS)
        target_compile_options(NamCore PRIVATE -march=native)
    endif()
endif()

# ── NAM only where it is actually used ────────────────────────────────────────
# The submodule links NamCore into GuitarAmpSim PUBLIC with --whole-archive, which
# forces the entire NAM core (+ Eigen + its mutable global registries) into EVERY
# consumer. Only the `amp` plugin uses NAM; the other eight don't. Embedding it
# everywhere bloated each .so by ~0.6 MB AND leaked NAM/Eigen global state out of
# every plugin, which is what interposed and crashed mod-host when two were loaded.
#
# Drop the whole-archive NamCore from GuitarAmpSim's link interface so plugins that
# never reference NAM don't pull it in at all. Re-attach it to the submodule's own
# example/test exes (which do use NAM) and, below, to the amp plugin only.
set_property(TARGET GuitarAmpSim PROPERTY INTERFACE_LINK_LIBRARIES "")
foreach(_E TestNam GuitarAmpExample)
    if(TARGET ${_E})
        target_link_libraries(${_E} PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,NamCore>)
    endif()
endforeach()

# ── System dependencies ───────────────────────────────────────────────────────
find_package(PkgConfig REQUIRED)
pkg_check_modules(LV2 REQUIRED lv2)
# NOTE: libsndfile was previously linked into the cab plugin for WAV IR loading,
# but the cab plugin only uses the embedded DefaultCabIR — no file I/O. Dropped
# to avoid an unnecessary runtime .so dependency on the device.

# ── Per-plugin optimization flags ─────────────────────────────────────────────
# Applied to all LV2 targets. Native builds get -march=native automatically
# from GuitarAmpSim; these flags tune the LV2 wrapper layer itself.
set(LV2_OPT_FLAGS -O3 -ffast-math ${GUITARAMP_ARCH_FLAGS})

# ── Helper macro: one LV2 plugin .so ─────────────────────────────────────────
function(add_lv2_plugin name src)
    add_library(${name} SHARED ${src})
    target_include_directories(${name} PRIVATE
        lv2/common
        ${LV2_INCLUDE_DIRS}
    )
    target_link_libraries(${name} PRIVATE GuitarAmpSim)
    target_compile_options(${name} PRIVATE ${LV2_OPT_FLAGS})
    # Prevent cross-plugin symbol interposition: export ONLY the LV2 entry points
    # (version script forces everything else local), bind internal refs to this
    # .so's own defs, and don't re-export static-archive symbols. The version
    # script is the decisive one — visibility/exclude-libs alone leaked NAM globals.
    target_link_options(${name} PRIVATE
        -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/lv2/export.map
        -Wl,-Bsymbolic
        -Wl,--exclude-libs,ALL)
    set_target_properties(${name} PROPERTIES PREFIX "" SUFFIX ".so")
endfunction()

# ── Plugin targets ─────────────────────────────────────────────────────────────
add_lv2_plugin(guitaramp_gate    lv2/gate/gate_plugin.cpp)
add_lv2_plugin(guitaramp_comp    lv2/comp/comp_plugin.cpp)
add_lv2_plugin(guitaramp_drive   lv2/drive/drive_plugin.cpp)
add_lv2_plugin(guitaramp_modfx   lv2/modfx/modfx_plugin.cpp)
add_lv2_plugin(guitaramp_delay   lv2/delay/delay_plugin.cpp)
add_lv2_plugin(guitaramp_reverb  lv2/reverb/reverb_plugin.cpp)
add_lv2_plugin(guitaramp_amp     lv2/amp/amp_plugin.cpp)
add_lv2_plugin(guitaramp_utility lv2/utility/utility_plugin.cpp)

add_lv2_plugin(guitaramp_cab lv2/cab/cab_plugin.cpp)

# Multi-era Muff-style fuzz. Uses GuitarAmpSim (EHXBigMuff via OverdriveFactory);
# no NAM, so it is NOT in the whole-archive NamCore list below.
add_lv2_plugin(guitaramp_fuzz lv2/fuzz/fuzz_plugin.cpp)

# Hex Forge super-block: hosts every Hex Chain DSP block in one reorderable chain.
# Embeds the amp (+ drive overdrive), so it joins the whole-archive NamCore list.
add_lv2_plugin(guitaramp_hexforge lv2/hexforge/hexforge_plugin.cpp)

# NAM is used only by the amp plugin (NamModel) and the drive plugin (OverdriveBlock
# -> NamOverdrive); Hex Forge embeds both. Give these — and only these — the
# whole-archived NamCore so the model-parser static initializers register.
# --whole-archive is required: those registrations live in objects nothing
# references by name otherwise.
#
# When GUITARAMP_DISABLE_NAM is set we do the opposite: the four plugins compile
# with -DGUITARAMP_NO_NAM (→ NamModelStub.h, no nam:: references) and we don't
# link NamCore at all. We also drop NamCore — and the submodule's NAM example
# exes — from the default build so the heavy Eigen archive isn't compiled and any
# stray NAM reference fails loudly at the plugin link instead of silently pulling
# the library back in.
if(GUITARAMP_DISABLE_NAM)
    foreach(_P guitaramp_amp guitaramp_drive guitaramp_cab guitaramp_hexforge)
        target_compile_definitions(${_P} PRIVATE GUITARAMP_NO_NAM)
    endforeach()
    # Do NOT sever GuitarAmpSim's NamCore dependency: GuitarAmpSim still compiles
    # NamModel.cpp and needs NamCore's headers (get_dsp.h) on its include path.
    # NamCore is simply never linked into our plugins — the stub (GUITARAMP_NO_NAM)
    # means they don't reference any NAM object, so none is pulled into their .so.
    # Skip the NAM example exes to save (emulated) build time.
    foreach(_E TestNam GuitarAmpExample)
        if(TARGET ${_E})
            set_target_properties(${_E} PROPERTIES EXCLUDE_FROM_ALL ON)
        endif()
    endforeach()
else()
    foreach(_P guitaramp_amp guitaramp_drive guitaramp_cab guitaramp_hexforge)
        target_link_libraries(${_P} PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,NamCore>)
    endforeach()
endif()

# ── Install into one LV2 bundle ───────────────────────────────────────────────
# RELATIVE to the install prefix — so `cmake --install --prefix <dir>` works.
# (An absolute path here would ignore --prefix and always target /usr/local.)
set(BUNDLE_DIR "lib/lv2/guitaramp-suite.lv2")

install(TARGETS
    guitaramp_gate guitaramp_comp guitaramp_drive guitaramp_modfx
    guitaramp_delay guitaramp_reverb guitaramp_amp guitaramp_cab
    guitaramp_utility guitaramp_fuzz guitaramp_hexforge
    DESTINATION ${BUNDLE_DIR}
)

# All TTL files are flat in lv2/ (LV2 bundles require manifest.ttl and all
# referenced TTLs in the same directory as the .so files)
install(FILES
    lv2/manifest.ttl
    lv2/gate.ttl
    lv2/comp.ttl
    lv2/drive.ttl
    lv2/modfx.ttl
    lv2/delay.ttl
    lv2/reverb.ttl
    lv2/amp.ttl
    lv2/cab.ttl
    lv2/utility.ttl
    lv2/fuzz.ttl
    lv2/hexforge.ttl
    DESTINATION ${BUNDLE_DIR}
)

# MOD-UI modgui resources: one dir per plugin, named exactly as that plugin's
# modgui:resourcesDirectory in its TTL (modgui-amp, modgui-cab, …). Each carries
# the stylesheet/icon/script, screenshot/thumbnail/logo, and knob/switch sprites.
foreach(_m amp cab drive delay gate comp modfx reverb utility fuzz hexforge)
    install(DIRECTORY lv2/modgui-${_m} DESTINATION ${BUNDLE_DIR})
endforeach()

# ── Offline analysis tool (host only, opt-in) ─────────────────────────────────
# nam_compare reports frequency-response / THD / loudness deltas between a .nam
# capture and the algorithmic amp model (tuning aid). It needs NAM, so it links
# NamCore explicitly. OFF by default so on-device (Pi) builds skip it entirely;
# enable on a desktop host with -DGUITARAMP_BUILD_TOOLS=ON.
option(GUITARAMP_BUILD_TOOLS "Build host-side analysis tools (nam_compare)" OFF)
if(GUITARAMP_BUILD_TOOLS AND NOT CMAKE_CROSSCOMPILING)
    add_executable(nam_compare tools/nam_compare.cpp)
    target_link_libraries(nam_compare PRIVATE GuitarAmpSim $<LINK_LIBRARY:WHOLE_ARCHIVE,NamCore>)
    set_target_properties(nam_compare PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools")

    # Per-effect DSP cost/denormal benchmark (host/native; see tools/bench.cpp).
    add_executable(bench tools/bench.cpp)
    target_link_libraries(bench PRIVATE GuitarAmpSim $<LINK_LIBRARY:WHOLE_ARCHIVE,NamCore>)
    set_target_properties(bench PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools")

    # Sunn Model T crunch sweep (THD vs volume) — see tools/sunn_sweep.cpp.
    add_executable(sunn_sweep tools/sunn_sweep.cpp)
    target_link_libraries(sunn_sweep PRIVATE GuitarAmpSim $<LINK_LIBRARY:WHOLE_ARCHIVE,NamCore>)
    set_target_properties(sunn_sweep PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools")

    # Sunn Model T envelope/"sputter" probe — see tools/sunn_envelope.cpp.
    add_executable(sunn_envelope tools/sunn_envelope.cpp)
    target_link_libraries(sunn_envelope PRIVATE GuitarAmpSim $<LINK_LIBRARY:WHOLE_ARCHIVE,NamCore>)
    set_target_properties(sunn_envelope PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools")
endif()
