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
