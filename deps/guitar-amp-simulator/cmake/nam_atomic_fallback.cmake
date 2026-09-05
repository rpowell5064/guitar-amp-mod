# FetchContent PATCH_COMMAND for NeuralAmpModelerCore (pin d65cf21).
#
# NAM's slimmable wavenet already carries TWO synchronization paths for its
# staged-model slot: std::atomic<std::shared_ptr<T>> (C++20 library feature)
# and, "for libc++", the C++11 std::atomic_* free functions on a plain
# shared_ptr. The gate is `#ifdef _LIBCPP_VERSION`, which strands libstdc++
# builds without the feature — GCC 9.4 (the Darkglass Anagram / KosmOS
# mod-plugin-builder toolchain) fails with "std::atomic requires a trivially
# copyable type". Widen the gate to the proper feature test so ANY standard
# library lacking __cpp_lib_atomic_shared_ptr takes the existing fallback.
# Preprocessor-identical on GCC 12+/MSVC C++20 (feature present). Idempotent.
foreach(_f "NAM/wavenet/slimmable.h" "NAM/wavenet/slimmable.cpp")
    file(READ "${_f}" _c)
    string(REPLACE
        "#ifdef _LIBCPP_VERSION"
        "#if defined(_LIBCPP_VERSION) || !defined(__cpp_lib_atomic_shared_ptr)"
        _c "${_c}")
    file(WRITE "${_f}" "${_c}")
endforeach()
message(STATUS "NAM patched: atomic<shared_ptr> fallback widened to feature test")

# ── Eigen 3.4.0 compat (2026-08-31): NAM uses Eigen::placeholders::lastN, which
# exists in its pinned Eigen master snapshot but NOT in released Eigen 3.4.0
# (e.g. Debian trixie libeigen3-dev via GAS_EIGEN_DIR). Eigen::lastN exists in
# both, so rewrite. Idempotent; byte-identical when already clean.
file(GLOB_RECURSE _nam_eigen_files "NAM/*.h" "NAM/*.cpp")
foreach(_f IN LISTS _nam_eigen_files)
    file(READ "${_f}" _c)
    string(REPLACE "Eigen::placeholders::lastN" "Eigen::lastN" _c "${_c}")
    file(WRITE "${_f}" "${_c}")
endforeach()
message(STATUS "NAM patched: Eigen::placeholders::lastN -> Eigen::lastN (Eigen 3.4.0 compat)")
