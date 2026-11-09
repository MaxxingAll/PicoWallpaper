# PicoWallpaper Development

## Architecture

The application is intentionally small and Windows-only:

- `src/main.cpp`: message loop, tray actions, per-monitor screens, playback, suspend/resume
- `src/decoder/mf_decoder.*`: synchronous Media Foundation source reader
- `src/platform/win32/d3d11_presenter.*`: shared D3D11 device and per-window video-processor output
- `src/platform/win32/desktop_injector.*`: WorkerW lookup and window placement
- `src/platform/win32/monitors.*`: connected monitors, their config keys and tray labels
- `src/platform/win32/power_governor.*`: fullscreen/maximized/lock/battery checks
- `src/core/config.*`: small INI configuration

The decoder gives the presenter an NV12 D3D11 texture and array-slice index,
plus the current media type's visible aperture, pixel aspect ratio, nominal
range and YUV matrix. Texture allocation dimensions are used to create the
processor; source cropping and aspect ratio use only the visible image.
The presenter creates an `ID3D11VideoProcessorInputView` on that slice and blits
directly to the swap-chain back buffer. No CPU pixel conversion, runtime shader
compiler, or intermediate shader texture is used.

Each monitor with a video is a "screen": its own child window of the WorkerW
covering that monitor, its own swap chain, decoder and clock, all on one D3D11
device. A monitor without a video has no window, so the WorkerW shows the Windows
wallpaper there. `SyncScreens` matches the windows to the connected monitors and
the `[monitors]` config at startup, on `WM_DISPLAYCHANGE`, after an Explorer
restart and after tray changes. Monitors are keyed by their device interface path,
so a choice stays with the physical monitor when display numbers change.

Malformed optional aperture data falls back to the full frame. Color conversions
are checked against the driver's video-processor enumerator, with limited-range
BT.709 as the YUV fallback (full-range RGB for RGB textures). The chosen conversion
is cached until the input format, requested color space or processor changes.

## Build

Use a Visual Studio 2022 developer shell:

```powershell
cmake -B build -S . -A x64
cmake --build build --config Release
```

No package manager is required. Release builds use `/MT`, link-time code
generation, and size optimization.

Debug builds retain the normal MSVC runtime checks and debug information;
`/O1`, `/GL` and `/LTCG` apply only to Release.

## Regression tests

```powershell
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

CTest exercises the production metadata parser and rectangle/color helpers
without a video decoder or display adapter. It covers padded textures, offset
and fractional apertures, aperture precedence, non-square pixels, full/studio
range and tagged matrices. Startup tests redirect HKCU inside the test process
to a temporary test key, which is removed afterward; they never change the real
Run entry. Checks remain active in Release builds.

Config migration tests use temporary folders and verify copying without overwriting
an existing configuration. Monitor tests cover display-number parsing, tray labels,
the `[monitors]` round trip (Unicode and `=` in paths, removed entries) and moving
the single-video setting to the main monitor. Isolated registry tests cover migration of enabled and
disabled autostart settings, alongside the existing reconciliation checks.

A COM enumerator stub also tests supported, unsupported and failed driver
conversion queries, including RGB/YUV fallback and an unsupported baseline.

Both configurations run in CI. The release workflow also runs CTest before
staging an executable. Set `-DBUILD_TESTING=OFF` to build only the application.

## Validation

Before a release:

1. Build on the oldest supported Windows 10 SDK and current Windows 11 SDK.
2. Test H.264 MP4 and VP9 WebM on Intel, AMD, and NVIDIA adapters where available.
3. Verify GPU Video Decode activity and seamless looping.
4. Exercise pause/resume for fullscreen, maximized, battery, lock, and sleep.
5. Restart Explorer and change the monitor layout while playback is active.
   With two monitors: different videos per monitor, one monitor removed (Windows
   wallpaper returns), a monitor unplugged and plugged back in.
6. Run the method in `docs/BENCHMARK.md` and record results.
7. Inspect imports and confirm every imported DLL is a Windows system DLL.
8. Verify the release checksum and provenance attestation.

The six-finding review fix and its outstanding hardware checks are recorded in
`docs/P2-VALIDATION.md`.
