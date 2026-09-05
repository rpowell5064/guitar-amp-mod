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

# ── Eigen lastN compat (fixed 2026-09-05): NAM writes Eigen::placeholders::lastN.
# Verified against both Eigens this project builds with:
#
#   spelling                      Eigen 3.4.0 (released)   NAM's bundled Eigen (master)
#   Eigen::lastN                  yes                      NO
#   Eigen::placeholders::lastN    NO (no such namespace)   yes
#   Eigen::indexing::lastN        yes                      yes
#
# So neither unqualified spelling builds everywhere, and rewriting to
# Eigen::lastN (as this did from 2026-08-31) fixed the released-Eigen build
# while breaking every build on NAM's bundled Eigen -- which is what CI, the
# Pi and Windows all use. Eigen::indexing is the namespace Eigen provides for
# exactly this purpose and is the one spelling valid in both, so normalise to
# it. Idempotent: any of the three spellings converges here.
file(GLOB_RECURSE _nam_eigen_files "NAM/*.h" "NAM/*.cpp")
foreach(_f IN LISTS _nam_eigen_files)
    file(READ "${_f}" _c)
    string(REPLACE "Eigen::indexing::lastN" "Eigen::placeholders::lastN" _c "${_c}")
    string(REPLACE "Eigen::lastN"           "Eigen::placeholders::lastN" _c "${_c}")
    string(REPLACE "Eigen::placeholders::lastN" "Eigen::indexing::lastN"  _c "${_c}")
    file(WRITE "${_f}" "${_c}")
endforeach()
message(STATUS "NAM patched: lastN -> Eigen::indexing::lastN (valid in Eigen 3.4.0 and master)")
