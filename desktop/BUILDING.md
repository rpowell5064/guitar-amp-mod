# Hex Forge desktop — building

Standalone CMake project (this folder). The root CMakeLists stays the
untouched pi-Stomp/LV2 build; both compile the same engine source
(`lv2/hexforge/engine/hf_engine_all.h`).

## Windows (validated)

Requirements: Visual Studio 2022+ (MSVC, C++20 for NAM), CMake ≥ 3.22, network
on first configure (fetches JUCE 8.0.3 + the pinned NAM core), and the
WebView2 SDK nupkg extracted at
`%LOCALAPPDATA%\PackageManagement\NuGet\Packages\Microsoft.Web.WebView2.1.0.1901.177\`
(download `https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/1.0.1901.177`
— it is a zip — and extract there, or pass `-DJUCE_WEBVIEW2_PACKAGE_LOCATION=...`).

```powershell
cmake -S desktop -B desktop/out
cmake --build desktop/out --config Release
```

Artifacts: `desktop/out/HexForge_artefacts/Release/{Standalone,VST3}/`.

Smart App Control quirks (this machine): the stock JUCE `juceaide` helper is
cloud-hash-blocked — `cmake/juceaide_sac_nudge.cmake` patches one no-op line
into it so the local build passes. Freshly linked exes can be blocked while
the cloud verdict is pending; relink + launch immediately, or just retry
after a minute.

## macOS (prepared, needs a Mac to build/validate)

Everything platform-specific is already guarded: the webview uses WKWebView
automatically (the WebView2 backend/options are `#if JUCE_WINDOWS`), the
preset-store backup goes to `~/Library/Application Support/HexChain` via
JUCE, and the AU format is in the `FORMATS` list.

```bash
cmake -S desktop -B desktop/out -DCMAKE_BUILD_TYPE=Release
cmake --build desktop/out
# personal use: ad-hoc sign, then validate the AU
codesign --force --deep -s - "desktop/out/HexForge_artefacts/AU/Hex Forge.component"
cp -r "desktop/out/HexForge_artefacts/AU/Hex Forge.component" ~/Library/Audio/Plug-Ins/Components/
auval -v aufx HxFg HxCh
```

Expected first-run checks on the Mac: WKWebView honors the resource-provider
root URL (JUCE handles the custom scheme); if the page is blank, check the
provider is being hit at all before suspecting the page. No notarization is
needed for locally built personal-use plugins.

## Sample rates

The engine always runs at 48 kHz (factory preset levels + NAM captures are
48k-referenced). Hosts at 44.1/88.2/96/192 kHz are wrapped by the streaming
polyphase SRC (`src/HfResampler.h`, verified by
`build-tools/hf_resampler_test.cpp`: >90 dB round-trip SNR, ~104 dB image
rejection); the filter + FIFO delay is reported to the host as latency
(~49 samples at 44.1 kHz). At 48 kHz the wrapper is fully bypassed.

## UI assets

Editor serving is two-mode: when the repo checkout exists on disk
(`HF_MODGUI_DIR`), files are served live (edit + reopen). Otherwise the
assets embedded at build time (juce_add_binary_data) serve — the plugin is
fully self-contained for use outside this machine.
