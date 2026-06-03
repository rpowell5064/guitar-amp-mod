# nam_compare — reference-vs-model amp analysis

`nam_compare` runs the **same excitation** through a reference `.nam` capture of a
real amp and through the algorithmic amp model **exactly as the LV2 plugin renders
it** (`AmpBlockExtended` preamp+tonestack → `PowerAmpProcessor`, no cab, no makeup),
then reports where they differ. The NAM capture is the ground truth for "what the
modeled amp should sound like"; the deltas tell us how to tune the model DSP.

It reports:

- **Frequency response** per 1/3-octave band, normalised to 500 Hz.
  `delta = model − NAM`; negative at HF ⇒ model too dark, positive ⇒ too bright.
- **THD vs drive** at ~110 Hz (LF tightness) and ~1 kHz (raw harmonic generation),
  across four input levels — flags "too clean" / "too saturated".
- **Loudness / clean gain** — output RMS and the makeup factor that would match NAM
  (informs `kModelMakeup[]` in `lv2/amp/amp_plugin.cpp`).

## Choosing reference captures (important)

The comparison is only meaningful if the capture matches what the model produces:

- **Amp-only, no cab.** The model path has no cabinet, so use a DI / load-box NAM
  capture (no IR baked in), not a mic'd "full rig" capture.
- **Comparable knob settings.** A NAM is one fixed snapshot. Note the capture's
  documented gain/EQ and pass the matching `--gain --bass --mid ...` flags (all
  default to noon = 0.5).
- **48 kHz.** NAM's standard rate; the tool does not resample and warns on mismatch.

Drop captures in `../nam_models/` (one per amp: Fender Deluxe, JCM800, 5150 III,
Sunn Model T, Orange Rockerverb 50).

## Building (Windows host)

The parent `CMakeLists.txt` builds the LV2 bundle and requires pkg-config/LV2
(Linux/Pi only). For host-side tuning on Windows, use the scratch project in
`../build-tools`, which compiles just this tool against the DSP submodule:

```powershell
$cmake = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
& $cmake -S ..\build-tools -B ..\build-tools\out -G "Visual Studio 18 2026" -A x64 `
    "-DFETCHCONTENT_SOURCE_DIR_NEURALAMPMODELERCORE=C:/Development/Projects/GuitarAmpSimulator/build/_deps/neuralampmodelercore-src"
& $cmake --build ..\build-tools\out --config Release --target nam_compare
```

(The `FETCHCONTENT_SOURCE_DIR_*` override reuses the already-downloaded NAM core
from the sibling repo so configure doesn't hit the network.)

On Linux/Pi the tool also builds from the parent CMake as the `nam_compare` target
(host/native builds only; skipped on cross-compiles).

## Usage

```
nam_compare --ref capture.nam --model <fender|marshall|evh|sunn|rockerverb>
            [--sr 48000] [--in di.wav] [--inlevel -18]
            [--gain --bass --mid --treble --presence --master --sag --channel --reson]
```

- `--in di.wav` uses a real DI recording as the spectral excitation (mono mixdown,
  rescaled to `--inlevel`) instead of internal pink noise. THD always uses synth tones.
- `--model` maps to the plugin's amp + power-amp defaults + tube type automatically.

Example:

```
nam_compare --ref ../nam_models/jcm800_noon.nam --model marshall --gain 0.5
```
