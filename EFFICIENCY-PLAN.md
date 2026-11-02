# EFFICIENCY-PLAN

Goal: a smaller executable and lower CPU, wake-up and GPU-memory use, with no feature change.
Base: `main` @ `0511172` (2026-11-02). Line numbers below refer to that commit.
Owner priority: tiny footprint and efficiency. A Rust rewrite was assessed and rejected (no gain); do not propose it.

## Rules for the agent

1. **Order:** work phase by phase, top to bottom. One branch per phase (`perf/p0-measure`, `perf/p1-size`,
   `perf/p3-cpu`, `perf/p4-gpu`, `perf/p5-decode`), one PR per branch. Do not merge; the owner merges after review.
2. **Identity:** GitHub writes only through your own GitHub App identity (agent-brain `AGENTS.md` §8). Never the owner's login.
3. **Builds:** the owner's PC has no MSVC/CMake. GitHub Actions CI is the build and test system. Never claim a local build.
4. **Evidence:** every task ends with its *Done when* checks. Record the CI run id, exact byte sizes and test
   results in the status table at the bottom. Hardware measurements (CPU, wake-ups, GPU memory, visuals) are done by
   the owner: leave those cells `Pending (owner)`; never estimate them as results.
5. **Owner decisions:** tasks marked **Gate D1/D2/D3** wait until the decision cell in the table is filled.
6. **YAGNI:** no new settings, INI keys, abstractions or refactors beyond the task. Each commit has a one-line reason.
   Conventional commit prefixes: `perf:`, `ci:`, `docs:`, `test:`.
7. **Unknowns:** write `UNCLEAR: <question>` in the status table instead of guessing; continue with the next task.
8. **Constraints that stay:** Windows 10/11 only, C++20/MSVC, no package manager, no bundled codecs, no runtime
   dependency outside Windows, Release exe ≤ 1,572,864 bytes, Debug and Release CTest green.
9. **New pure logic gets a unit test** in `tests/regression_tests.cpp` (the test target already links the presenter
   and format helpers; add `src/core/playback_clock.cpp` to it when P3.1 needs it).

## Phase 0 — Measurement (branch `perf/p0-measure`)

### P0.1 Size breakdown in CI
- **Where:** `CMakeLists.txt` link options (Release), `.github/workflows/build.yml` (Release job).
- **Change:** add `/MAP` to the Release link options. Add a CI step (Release only) that finds `dumpbin.exe` via
  `vswhere` (no new third-party action) and prints `dumpbin /headers` section sizes and `dumpbin /imports` DLL names
  for `build/Release/LiteWallpaper-MF.exe`. Upload the `.map` file with `actions/upload-artifact@v4`, retention 7 days.
- **Done when:** the CI log shows section sizes and the imported DLL list; the map artifact downloads.

### P0.2 Fix the benchmark method
- **Where:** `docs/BENCHMARK.md`.
- **Change:** remove the upstream "settings panel" wording (lines 13, 32: the fork is tray-only; select the clip via
  tray → Choose video). Remove the `--memprobe` sentence (line 43; the mode no longer exists). Add three metrics to
  the results table: wake-ups per second while playing and while paused (WPR CPU Usage (Precise) context switches,
  or Process Explorer "Context Switch Delta"), frame-time spread while playing (WPR/PresentMon), and **private commit**
  while paused (the working-set trim in `main.cpp:179` makes working set alone misleading). Add a second reference
  run: the same clip re-encoded at 60 fps, played with the 60 FPS cap, so P5.2 can quote the measured 30/60 ratio.
- **Done when:** the doc describes only features that exist; the results table has the new rows and a 60 fps
  column, all `Pending`.

### P0.3 Baseline (owner)
- The owner runs `docs/BENCHMARK.md` on `main` before Phase 3 starts. The agent does nothing here.

## Phase 1 — Executable size (branch `perf/p1-size`)

Estimate (assumed): 583 KB → about 150–250 KB after P1.1–P1.4.

### P1.1 Remove the duplicate icon resource
- **Where:** `src/app.rc` line 2 (`101 ICON "../assets/app.ico"`).
- **Why:** the same 179,520-byte icon is embedded twice; ID 101 is referenced nowhere (`window_class.hIcon` and the
  tray use ID 1).
- **Change:** delete line 2.
- **Done when:** CI Release size drops by roughly 175 KB (record exact bytes); the owner confirms the Explorer and
  tray icons still show.

### P1.2 Recompress the icon
- **Where:** `assets/app.ico` (7 images: 16/24/32/48/64/128 as uncompressed 32-bit BMP, 256 as a 77 KB PNG).
- **Change:** rebuild the ICO with 16, 24, 32, 48 as 32-bit BMP and 64, 128, 256 as PNG; optimise the PNGs
  losslessly. Use any local tool (e.g. a Python script with Pillow); the tool is not added to the build.
- **Done when:** `app.ico` ≤ 60 KB with all 7 sizes present (list them in the PR); the owner checks the icon at
  100/150/200 % display scaling.

### P1.3 Drop printf-family formatting
- **Where:** `src/decoder/mf_decoder.cpp:79-84` (`HResultHex` uses `std::snprintf`), `src/core/config.cpp:43`
  (`WriteInt` uses `swprintf_s`).
- **Change:** replace both with small hand-written formatters (hex for HRESULT, decimal for int). Behaviour and
  output text unchanged.
- **Done when:** the P0.1 map no longer lists CRT printf/stdio formatting objects; the size delta is recorded;
  tests green.

### P1.4 Hybrid CRT — **Gate D1**
- **Where:** `CMakeLists.txt`.
- **Change:** keep the static VC runtime (`/MT`), link the Universal CRT dynamically: Release adds
  `/NODEFAULTLIB:libucrt.lib ucrt.lib`, Debug `/NODEFAULTLIB:libucrtd.lib ucrtd.lib`. `ucrtbase.dll` ships with
  Windows 10/11, so no redistributable is needed.
- **Done when:** imports (P0.1) show only Windows system DLLs plus `api-ms-win-crt-*`; no `vcruntime*`/`msvcp*`;
  size delta recorded; the owner runs the exe on a clean Windows 10 22H2 install.

## Phase 3 — CPU and wake-ups (branch `perf/p3-cpu`)

Starts after the P0.3 baseline. Each task needs an owner before/after measurement.

### P3.1 One wake-up per frame with a high-resolution timer
- **Where:** `src/main.cpp:564-576` (playing branch of the loop), `src/core/playback_clock.*`.
- **Why:** the loop caps each wait at 16 ms, waits on the frame-latency waitable (which also fires right after
  each vblank) and then adds 1 ms top-ups. With the default 15.6 ms timer this likely gives 3–4 wake-ups per frame
  and quantised frame times (judder).
- **Change:** create one timer with `CreateWaitableTimerExW(..., CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
  TIMER_ALL_ACCESS)`, falling back to a normal waitable timer if that fails. Add `PlaybackClock::MicrosUntilNextFrame()`
  (pure, unit-tested). After each loop pass, arm the timer for `min(next frame, next 5-second recovery check)` and
  wait with `MsgWaitForMultipleObjectsEx(1, &timer, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE)`. Remove the 16 ms cap
  and the 1 ms rounding. Remove the frame-latency waitable from the idle wait; if kept at all, wait on it only right
  before rendering a due frame, with a timeout of one frame interval. Do not call `timeBeginPeriod`.
- **Done when:** unit tests for the deadline math pass; the owner's trace shows about 1 wake-up per presented frame
  (plus input) and a smaller frame-time spread; pause, resume, recovery and Explorer restart behave as before.

### P3.2 Quieter governor while paused
- **Where:** `src/platform/win32/power_governor.cpp:46-48`, `:104-117`; `src/main.cpp:555-562`.
- **Change:**
  - Remove the battery poll at line 48; `HandlePowerChange` (line 76) already updates it from `WM_POWERBROADCAST`.
  - In the `EnumWindows` callback (line 104) check `IsWindowVisible` before `GetClassNameW`.
  - Paused loop: locked/suspended (`Sleeping`) or manual pause → wait for messages with a 5000 ms timeout
    (keeps the recovery check); fullscreen/maximised/battery pause → poll every 1000 ms; make the 250 ms cache
    at line 46 match the poll interval used.
- **Done when:** the owner measures ≤ 1 wake-up/s while auto-paused and ≤ 0.2/s while locked; playback resumes within
  ≤ 1 s after un-maximising, unlocking or plugging in AC.

### P3.3 Set video-processor state only when it changes
- **Where:** `src/platform/win32/d3d11_presenter.cpp` around lines 285-300.
- **Change:** set background colour, output target rect, frame format, output colour space and auto-processing once
  after `CreateVideoProcessor`. Set source/destination rects and the stream colour space only when they differ
  from the cached last values. Reset the cache wherever the processor is recreated.
- **Done when:** a frame normally issues only `VideoProcessorBlt`; tests green; the owner sees identical output.

## Phase 4 — GPU memory (branch `perf/p4-gpu`)

### P4.1 Release the GPU during automatic pauses — **Gate D3**
- **Where:** `src/main.cpp:172-187` (`EnterSuspend`/`LeaveSuspend`), `:555-562`, the recovery timer `:534-550`.
- **Why:** during fullscreen/maximised/lock/sleep pauses the desktop is not visible, yet the device, the decoder
  pool and two desktop-sized back buffers stay allocated (e.g. 2 × 33 MB at 4K).
- **Change:** governor pauses (`Paused`, `Occluded`, `Sleeping`) do a deep suspend: close the decoder, hide the render
  window, `g_presenter.Cleanup()`, keep the window attached. Leaving a deep suspend calls `RebuildGraphics(true)`
  (resumes at `g_playback_seconds`). Manual pause keeps today's behaviour (still frame stays visible). The recovery
  timer must not rebuild graphics while deep-suspended; `WM_DISPLAYCHANGE` must still resize the window.
- **Done when:** the owner measures ≈ 0 dedicated/shared GPU memory for the process during a fullscreen pause; video
  returns ≤ 300 ms after un-maximising at the same position; Explorer restart and GPU reset during a deep pause
  recover.

### P4.2 Back buffer no larger than the video — **Gate D2**
- **Where:** `d3d11_presenter.cpp` `CreateSwapChain` (149), `Resize` (313), `RenderFrame`; `main.cpp`
  `ResizeToDesktop` (266).
- **Change:** separate window size from back-buffer size. Add a pure `ChooseBackBufferSize(format, desktop_w,
  desktop_h, scaling_mode)` (unit-tested) that scales the desktop size down by
  `s = min(1, max(source_w / dest_w, source_h / dest_h))`, rounded up, with the rects from `CalculateVideoRectangles`.
  Keep `DXGI_SCALING_STRETCH` so DWM stretches the buffer to the window. Recompute on video change, scaling-mode
  change and desktop resize.
- **Done when:** tests cover 1080p video on 4K (→ 1920×1080), 4K video on 1080p (→ 1920×1080), cover/fit/stretch
  on ultrawide and two-monitor spans; the owner compares screenshots and records GPU memory before/after.

### P4.3 Decoder surface pool — experiment, after the baseline
- **Where:** `src/decoder/mf_decoder.cpp` `Open` (around 248-290).
- **Change:** only if the P0.3 baseline shows GPU memory > 50 MB: try lowering the decoder's output pool
  (`IMFSourceReaderEx::GetTransformForStream` → output stream attributes → `MF_SA_MINIMUM_OUTPUT_SAMPLE_COUNT`).
  `UNCLEAR:` is an acceptable result.
- **Done when:** a before/after table from the owner; keep the change only if it saves ≥ 10 MB without stutter or
  loop glitches; otherwise revert it and record why.

## Phase 5 — Decode (branch `perf/p5-decode`, optional)

### P5.1 Native 10-bit output
- **Where:** `src/decoder/mf_decoder.cpp:126` (`ConfigureVideoType` always requests NV12).
- **Change:** when the native type is 10-bit (HEVC Main10, VP9 profile 2, AV1 10-bit via `MF_MT_VIDEO_PROFILE`),
  request `MFVideoFormat_P010` first, then NV12, then ARGB32. The presenter already accepts P010.
- **Done when:** the owner's 10-bit clip logs P010 output and looks the same; 8-bit clips are unchanged.

### P5.2 README guidance: 30 fps or 60 fps clips
- **Where:** `README.md`.
- **Why:** decoding always runs at the clip's own frame rate. The FPS cap and battery mode only reduce how often
  frames are shown, so a 60 fps clip with a 30 fps cap still decodes 60 frames per second.
- **Change:** one short section with two options and the matching tray setting:

  | Option | Encode the clip at | Tray → FPS cap | Effect |
  |---|---|---|---|
  | Low power (suggestion: default) | 30 fps | 30 FPS or Native | Lowest decode and present work |
  | Smooth | 60 fps | 60 FPS or Native | Smoother motion; Estimate (assumed): about 2× the decode work of 30 fps |

  State that a mismatch (60 fps clip with a 30 FPS cap) costs the 60 fps decode work but shows only 30 fps, and that
  on battery the cap drops to 15 FPS without lowering the decode work. Recommend H.264 or HEVC.
- **Done when:** the section and table exist; no other README changes. If the owner's P0.3 baseline includes both a
  30 fps and a 60 fps run, replace the 2× estimate with the measured ratio.

## Out of scope
Rust rewrite, per-monitor video, playlists, audio, HDR, FIXTODO 6 (tray double-click), release tagging.

## Owner decisions

| Id | Question | Decision |
|---|---|---|
| D1 | Hybrid CRT: link `ucrtbase.dll` (part of Windows 10/11) instead of a fully static CRT? | Pending |
| D2 | Back buffer ≤ video size: accept DWM's slightly softer upscale for less GPU memory and bandwidth? | Pending |
| D3 | Deep pause: accept a brief static Windows wallpaper (~100–300 ms) when playback resumes? | Pending |

## Status

| Task | Branch / PR | CI run | Result (bytes, tests) | Owner measurement |
|---|---|---|---|---|
| P0.1 | | | | — |
| P0.2 | | | | — |
| P0.3 | — | — | — | Pending (owner) |
| P1.1 | | | | Pending (owner) |
| P1.2 | | | | Pending (owner) |
| P1.3 | | | | — |
| P1.4 | | | | Gate D1 |
| P3.1 | | | | Pending (owner) |
| P3.2 | | | | Pending (owner) |
| P3.3 | | | | Pending (owner) |
| P4.1 | | | | Gate D3 |
| P4.2 | | | | Gate D2 |
| P4.3 | | | | Pending (owner) |
| P5.1 | | | | Pending (owner) |
| P5.2 | | | | — |
