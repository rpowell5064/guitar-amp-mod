# Hex Chain native X11 UI (Zynthian VNC, desktop Linux LV2 hosts).
#
# Included by the root CMakeLists.txt ONLY with -DHEXCHAIN_X11UI=ON. The default
# build (pi-Stomp / MOD / PatchStorage) never configures this directory, links no
# X11 or cairo, and installs the bundle byte-identical to before.
#
# Depends on libx11-dev + libcairo2-dev. Port tables and the UI TTL are generated
# and committed: re-run `python build-tools/gen_ui_ports.py` after any TTL edit or
# after adding ui/plugins/<key>_ui.cpp.

enable_language(C)   # pugl is C; the root project only enables CXX
find_package(PkgConfig REQUIRED)
pkg_check_modules(HC_GFX REQUIRED x11 cairo)

# ── pugl (vendored, ISC; X11 + cairo backends only — see third_party/pugl) ──
set(PUGL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/pugl)
add_library(hc_pugl STATIC
    ${PUGL_DIR}/src/common.c
    ${PUGL_DIR}/src/internal.c
    ${PUGL_DIR}/src/x11.c
    ${PUGL_DIR}/src/x11_cairo.c
    ${PUGL_DIR}/src/x11_stub.c)
# Only libX11 + cairo: no Xrandr / Xsync / Xcursor, so a stock Zynthian or Pi OS
# image needs nothing beyond what any X desktop already has.
target_compile_definitions(hc_pugl
    PUBLIC  PUGL_STATIC
    PRIVATE USE_XRANDR=0 USE_XSYNC=0 USE_XCURSOR=0 _POSIX_C_SOURCE=200809L)
target_include_directories(hc_pugl PUBLIC ${PUGL_DIR}/include ${HC_GFX_INCLUDE_DIRS})
set_target_properties(hc_pugl PROPERTIES
    C_STANDARD 99
    POSITION_INDEPENDENT_CODE ON
    C_VISIBILITY_PRESET hidden)

# ── Core + per-plugin panels ──
file(GLOB HC_UI_PANELS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/plugins/*_ui.cpp)
set(HC_UI_CORE
    ${CMAKE_CURRENT_SOURCE_DIR}/core/hc_draw.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/hc_generic.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/hc_json.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/hc_kit.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/hc_panel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/hc_registry.cpp)
set(HC_UI_INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/generated
    ${HC_GFX_INCLUDE_DIRS})

# Panels register themselves from static initialisers, so they are compiled straight
# into each binary (a static library would let the linker drop them).
add_library(hexchain_ui SHARED
    ${CMAKE_CURRENT_SOURCE_DIR}/core/hc_lv2ui.cpp ${HC_UI_CORE} ${HC_UI_PANELS})
target_include_directories(hexchain_ui PRIVATE ${HC_UI_INCLUDES} ${LV2_INCLUDE_DIRS})
target_link_libraries(hexchain_ui PRIVATE hc_pugl ${HC_GFX_LINK_LIBRARIES})
target_compile_options(hexchain_ui PRIVATE -O2)
set_target_properties(hexchain_ui PROPERTIES
    PREFIX "" SUFFIX ".so"
    CXX_STANDARD 17
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)
# Same symbol isolation as the DSP plugins (see lv2/export.map): export only the
# entry point, so pugl/cairo glue can never interpose across plugins in one host.
target_link_options(hexchain_ui PRIVATE
    -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/export.map
    -Wl,-Bsymbolic
    -Wl,--exclude-libs,ALL)

# Headless PNG renderer for reviewing conversions (not installed).
add_executable(hc_render tools/hc_render.cpp ${HC_UI_CORE} ${HC_UI_PANELS})
target_include_directories(hc_render PRIVATE ${HC_UI_INCLUDES})
target_link_libraries(hc_render PRIVATE ${HC_GFX_LINK_LIBRARIES})
set_target_properties(hc_render PROPERTIES CXX_STANDARD 17)
