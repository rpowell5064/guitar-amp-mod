######################################
#
# guitaramp-suite  (Hex Chain — MOD Dwarf / mod-plugin-builder package)
#
# Copy this file to  mod-plugin-builder/plugins/package/guitaramp-suite/guitaramp-suite.mk
# then:  ./build moddwarf guitaramp-suite
# Output bundle:  ~/mod-workdir/moddwarf/plugins/guitaramp-suite.lv2
#
# Status: UNVERIFIED on device — authored without a buildroot/Dwarf to test on.
# The one part that needs on-device iteration is the NAM neural core (FetchContent);
# see the NAM CORE section below and build-tools/moddwarf/README.md.
#
######################################

# Pinned to the v1.4.0 release commit. Bump to a newer SHA/tag as the suite moves on.
GUITARAMP_SUITE_VERSION = d246f18ab9dce471327c3ec1586257fe3b545de7
GUITARAMP_SUITE_SITE = https://github.com/rpowell5064/guitar-amp-mod.git
GUITARAMP_SUITE_SITE_METHOD = git

# The whole suite installs as one MOD bundle (11 plugins inside one .lv2 dir).
GUITARAMP_SUITE_BUNDLES = guitaramp-suite.lv2

# LV2 headers come from the buildroot 'lv2' package.
GUITARAMP_SUITE_DEPENDENCIES = lv2

# Generic aarch64 (Cortex-A53 = ARMv8-A). GUITARAMP_PORTABLE strips the DSP
# submodule's hard-coded -march=native so buildroot's own toolchain flags
# (TARGET_OPTIMIZATION, the moddwarf -mcpu) drive codegen. Do NOT keep the Pi-5
# -mtune=cortex-a76 path — that's selected only when PORTABLE is OFF.
GUITARAMP_SUITE_CONF_OPTS = -DGUITARAMP_PORTABLE=ON

# ── NAM CORE (the one tricky dependency) ─────────────────────────────────────
# The build FetchContent-pulls NeuralAmpModelerCore (pinned to d65cf21, which has
# nam::DSP::ResetAndPrewarm) + its Eigen submodule at CMake *configure* time.
# buildroot's build phase is usually offline, so pick ONE:
#
#   A) mpb build has network (common): leave it as-is — FetchContent just works.
#
#   B) Offline build: pre-clone the core (download phase HAS network) and point
#      CMake at it so FetchContent uses the local copy instead of downloading.
#      Uncomment the hook + the override below:
#
# define GUITARAMP_SUITE_CLONE_NAMCORE
# 	git clone --recurse-submodules https://github.com/sdatkinson/NeuralAmpModelerCore.git $(@D)/nam-core && \
# 	cd $(@D)/nam-core && git checkout d65cf2114e4a9e083292b235af1a24789d6fe128 && git submodule update --init --recursive
# endef
# GUITARAMP_SUITE_POST_DOWNLOAD_HOOKS += GUITARAMP_SUITE_CLONE_NAMCORE
# GUITARAMP_SUITE_CONF_OPTS += -DFETCHCONTENT_SOURCE_DIR_NEURALAMPMODELERCORE=$(@D)/nam-core
#
#   C) Fully reproducible: add NeuralAmpModelerCore (recursive, incl. Eigen) as a
#      git submodule of this repo, then use MOD_PLUGIN_BUILDER_DOWNLOAD_WITH_SUBMODULES
#      (like aidadsp-lv2) + the override in (B). Cleanest, but a repo change — see README.
# GUITARAMP_SUITE_PRE_DOWNLOAD_HOOKS += MOD_PLUGIN_BUILDER_DOWNLOAD_WITH_SUBMODULES

# The CMake install rules already place everything in
# lib/lv2/guitaramp-suite.lv2 under the prefix, so the default cmake-package
# install (DESTDIR=$(TARGET_DIR), prefix /usr) lands it at the correct
# /usr/lib/lv2/guitaramp-suite.lv2 — no custom INSTALL_TARGET_CMDS needed.

$(eval $(cmake-package))
