# Third-Party Notices

The Hex Chain guitar-amp suite (GPL-3.0-or-later + commercial — see
[`LICENSING.md`](LICENSING.md)) builds on the following third-party components,
each under its own license. These licenses are GPL-compatible and permit use in
both the open-source and commercial distributions; their notices must be retained.

| Component | Used for | License |
|-----------|----------|---------|
| [NeuralAmpModelerCore](https://github.com/sdatkinson/NeuralAmpModelerCore) | NAM (.nam) neural model inference | MIT |
| [Eigen](https://eigen.tuxfamily.org) | linear algebra (within NAM core) | MPL-2.0 |
| [nlohmann/json](https://github.com/nlohmann/json) | NAM model JSON parsing | MIT |
| [LV2](https://lv2plug.in) (lv2 headers) | plugin host API | ISC |

These are fetched at build time (CMake `FetchContent`) and are **not** part of this
repository's source tree; each retains its own upstream license. MPL-2.0 (Eigen) is
file-level copyleft — this project does not modify Eigen's files. MIT/ISC are
permissive. None of these restrict the dual-licensing of this project's own code.

The `deps/guitar-amp-simulator` submodule (GuitarAmpSimulator) is also authored by
Ryan Powell and is licensed under the same GPL-3.0-or-later + commercial terms.
