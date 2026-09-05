# tools/

The measurement, fitting and verification harnesses that were once here now live
in a separate private repository. They are development instrumentation only —
nothing in the shipped plugins depends on them, and the public build does not
reference them.

What remains public is everything needed to **build, gate and deploy** the suite:

* `build-tools/gen_hexforge.py` — generates the port enum, the TTL and the modgui
* `build-tools/gen_hexforge_presets.py`, `gen_anagram.py` — generated tables/bundles
* `build-tools/hexforge_golden.cpp` — golden-render regression gate (128 presets)
* `build-tools/hexforge_migrate_test.cpp` — preset-blob migration round-trip
* `build-tools/hf_resampler_test.cpp`, `hexforge_engine_standalone.cpp`
* `build-tools/deploy_pi.sh`, `reload_pi.sh`, `build_verify_pi.sh` — device deploy

The amp models ship with their voicing constants in the source, as always; how
those constants were arrived at is not part of this repository.
