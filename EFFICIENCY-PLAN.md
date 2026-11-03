# EFFICIENCY-PLAN

Goal: complete the PicoWallpaper identity migration (Phase R), then a smaller executable and lower CPU, wake-up and GPU-memory
use, with no feature change.
Base: `main` @ `0511172` (2026-11-02). Line numbers below refer to that commit; after Phase R, locate code by the
function names given (the rename shifts a few lines).
Owner priority: tiny footprint and efficiency. A Rust rewrite was assessed and rejected (no gain); do not propose it.

## Rules for the agent

1. **Order:** work phase by phase, top to bottom. One branch per phase (`chore/rename-picowallpaper`,
   `perf/p0-measure`, `perf/p1-size`, `perf/p3-cpu`, `perf/p4-gpu`, `perf/p5-decode`), one PR per branch, each PR
   targeting `main`. Do not merge; the owner merges after review. If the previous phase's PR is not merged yet,
   branch from that phase's branch and say so at the top of the PR description (the owner merges in order).
2. **Identity:** GitHub writes only through your own GitHub App identity (agent-brain `AGENTS.md` §8). Never the owner's login.
3. **Builds:** the owner's PC has no MSVC/CMake. GitHub Actions CI is the build and test system. Never claim a local build.
4. **Evidence:** every task ends with its *Done when* checks. Record the CI run id, exact byte sizes and test
   results in the status table at the bottom. Hardware measurements (CPU, wake-ups, GPU memory, visuals) are done by
   the owner: leave those cells `Pending (owner)`; never estimate them as results.
5. **Owner decisions:** D1–D3 are decided (see the table). Tasks marked *dropped* must not be implemented.
6. **YAGNI:** no new settings, INI keys, abstractions or refactors beyond the task. Each commit has a one-line reason.
   Conventional commit prefixes: `perf:`, `ci:`, `docs:`, `test:`, `chore:` (rename).
7. **Unknowns:** write `UNCLEAR: <question>` in the status table instead of guessing; continue with the next task.
8. **Constraints that stay:** Windows 10/11 only, C++20/MSVC, no package manager, no bundled codecs, no runtime
   dependency outside Windows, Release exe ≤ 1,572,864 bytes, Debug and Release CTest green.
9. **New pure logic gets a unit test** in `tests/regression_tests.cpp` (the test target already links the presenter
   and format helpers; add `src/core/playback_clock.cpp` to it when P3.1 needs it).

## Phase R — Rename to PicoWallpaper (branch `chore/rename-picowallpaper`)

The repository and application use PicoWallpaper. R1 updates the build, runtime identity,
configuration, autostart and public documentation together; CI and owner acceptance are tracked below.

### R1 Rename everything
- **Current names and scope:**

  | Identity | Current name | Where |
  |---|---|---|
  | Executable and linker map | `PicoWallpaper.exe`, `PicoWallpaper.map` | CMake output and /MAP; both workflows, size checks, checksum, attestation, release assets and downloadable CI artifacts |
  | CMake project and targets | `PicoWallpaper`, `PicoWallpaperTests` | `CMakeLists.txt` |
  | Namespace | `picowp` | `src/` and `tests/` |
  | Window classes | `PicoWallpaperWindow`, `PicoWallpaperControl`, `PicoWallpaper_MsgReceiver` | Main window and governor share the render-class constant |
  | Titles, tray tooltip and debug-log prefix | `PicoWallpaper` | Main, tray and decoder |
  | Instance mutex | `Local\PicoWallpaper.Instance` | Main |
  | Configuration | `%APPDATA%\PicoWallpaper\config.ini` | Config and tests |
  | Run value | `PicoWallpaper` | Startup and tests |
  | Public docs | PicoWallpaper | README, ROADMAP, DEVELOPMENT, BENCHMARK, this plan and the two current executable checks in FIXTODO |

- **Migration for existing users (required):**
  1. *Config:* on load, if the new configuration does not exist and the legacy configuration does,
     copy it (`CopyFileW`, fail if exists). Never delete or change the old folder. Keep the copy in a small
     function taking both folder paths, tested with temporary folders.
  2. *Autostart:* before `ReconcileRunAtStartup`, remove any legacy Run value; if the migrated setting
     `run_on_startup` is on, write the new `PicoWallpaper` value. Then reconcile as before. Isolated-registry
     tests cover legacy present + enabled → only new value; legacy present + disabled → no value; and
     no legacy → unchanged behavior.
  3. Legacy identity literals remain only as clearly named migration constants and their tests.
- **Keep unchanged:** upstream attribution and links in README and ROADMAP, `LICENSE`, historical review
  text in `FIXTODO.md` (except its two current executable checks) and `docs/P2-VALIDATION.md`.
  Do not rename the GitHub repository or the owner's local folders.
- **Done when:** Debug and Release CI pass, migration tests pass and the downloadable CI executable is
  `build/Release/PicoWallpaper.exe`. The obsolete-identity inventory must contain only upstream attribution,
  legacy migration constants and their tests, and the historical files above (list remaining hits in the PR).
  Owner acceptance remains pending: settings and autostart carry over, the tray shows PicoWallpaper,
  and only one Run value exists.

## Phase 0 — Measurement (branch `perf/p0-measure`)

### P0.1 Size breakdown in CI
- **Where:** `CMakeLists.txt` link options (Release), `.github/workflows/build.yml` (Release job).
- **Change:** add `/MAP` to the Release link options. Add a CI step (Release only) that finds `dumpbin.exe` via
  `vswhere` (no new third-party action) and prints `dumpbin /headers` section sizes and `dumpbin /imports` DLL names
  for `build/Release/PicoWallpaper.exe` (the current name). Upload the `.map` file with `actions/upload-artifact@v4`, retention 7 days.
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

Estimate (assumed): 583 KB → about 250–300 KB after P1.1–P1.3. The CRT stays fully static (D1: No).

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

### P1.4 Hybrid CRT — dropped (owner D1: No)
Do not change the CRT linkage. The exe keeps the fully static CRT (`/MT`) and imports no `api-ms-win-crt-*` DLLs.

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

### P4.1 Release the GPU during automatic pauses, show a snapshot (owner D3: Yes, with snapshot)
- **Where:** `src/main.cpp:172-187` (`EnterSuspend`/`LeaveSuspend`), `:189-241` (`RenderNextFrame`), `:266-282`
  (`ResizeToDesktop`), `:284-314` (`RebuildGraphics`), `:393-400` (`WindowProc`), `:555-562`, the recovery timer
  `:534-550`; `src/platform/win32/d3d11_presenter.*`.
- **Why:** during fullscreen/maximised/lock/sleep pauses the device, the decoder pool and two desktop-sized back
  buffers stay allocated (e.g. 2 × 33 MB at 4K). With pause-on-maximised the desktop can still be visible on another
  monitor, so the wallpaper must keep its picture.
- **Owner requirement:** the normal Windows wallpaper must never become visible: not while paused, not while the
  GPU is released, and not during resume. The paused picture is the last frame at full desktop resolution (D2: No,
  no reduced quality).
- **Change:**
  1. **Capture** — add `D3D11Presenter::CaptureFrame(const VideoFrame&, int scaling_mode, <snapshot out>)`. With
     `FLIP_DISCARD` the presented buffer is undefined, so render `g_frame` again into the back buffer (no `Present`),
     `CopyResource` it to a `D3D11_USAGE_STAGING` BGRA texture, `Map`, and copy the rows into a top-down 32-bit
     `CreateDIBSection` bitmap owned by `main.cpp`. Release the staging texture right after.
  2. **Deep suspend** — governor pauses (`Paused`, `Occluded`, `Sleeping`) only: capture while the decoder and
     `g_frame` are still valid, then close the decoder, `g_presenter.Cleanup()`, keep the render window **shown and
     attached**, and repaint it from the snapshot (`InvalidateRect` + `UpdateWindow`). If capture fails, fall back
     to today's suspend (keep the device and the still frame); never release the GPU without a picture.
  3. **Painting** — `WindowProc` handles `WM_ERASEBKGND` (return 1) and `WM_PAINT`: while a snapshot exists,
     `StretchDIBits` it to the client area (stretches if the desktop size changed during the pause).
  4. **Resume** — `RebuildGraphics(true)` and `ResizeToDesktop` must not `ShowWindow(SW_HIDE)` while a snapshot
     exists (today they hide at lines 280 and 296). Playback resumes at `g_playback_seconds`, so the first new frame
     matches the snapshot. Free the snapshot only after the first successful `Present`.
  5. **Recovery** — the recovery timer must not rebuild graphics while deep-suspended. If Explorer restarts during
     the pause the render window is recreated without the snapshot; that case may show the wallpaper until resume
     (document it).
  6. **Manual pause** keeps today's behaviour (device and still frame stay).
  7. If GDI painting on the former swap-chain window flashes black or shows the wallpaper on the owner's hardware,
     paint the snapshot in a separate sibling child window placed over the render window instead; write `UNCLEAR:`
     with the observation before switching.
- **Cost:** the snapshot uses CPU memory of desktop width × height × 4 bytes while paused (Estimate (assumed): 8 MB at
  1080p, 33 MB at 4K, 17 MB for a 2 × 1080p span), instead of the GPU memory it replaces. Update the paused-memory
  target in `docs/BENCHMARK.md` to "< 1 MB + snapshot size".
- **Done when:** the owner sees no wallpaper flash and no black frame when pausing, while paused (second monitor with
  pause-on-maximised) and when resuming; ≈ 0 dedicated/shared GPU memory for the process during a fullscreen pause;
  video resumes ≤ 300 ms after un-maximising at the snapshot's position; capture failure falls back to the normal
  suspend; Explorer restart and GPU reset during a deep pause recover.

### P4.2 Back buffer no larger than the video — dropped (owner D2: No)
Do not change the back-buffer size. The swap chain stays at full desktop size.

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
| D1 | Hybrid CRT: link `ucrtbase.dll` (part of Windows 10/11) instead of a fully static CRT? | **No** (2026-11-02): P1.4 dropped |
| D2 | Back buffer ≤ video size: accept DWM's slightly softer upscale for less GPU memory and bandwidth? | **No** (2026-11-02): P4.2 dropped |
| D3 | Deep pause: release the GPU during automatic pauses? | **Yes, with a full-resolution snapshot** (2026-11-02): the normal wallpaper must never show; see P4.1 |

## Status

| Task | Branch / PR | CI run | Result (bytes, tests) | Owner measurement |
|---|---|---|---|---|
| R1 | | | | Pending (owner) |
| P0.1 | `perf/p0-measure` / #1 | [41426394525](https://github.com/MaxxingAll/PicoWallpaper/actions/runs/41426394525) | 583,168 → 583,168 bytes. Debug/Release CTest passed. Section sizes and imports logged; map downloaded (374,136 bytes). | — |
| P0.2 | `perf/p0-measure` / #1 | [41426508723](https://github.com/MaxxingAll/PicoWallpaper/actions/runs/41426508723) | 583,168 → 583,168 bytes. Debug/Release CTest passed. | — |
| P0.3 | — | — | — | Pending (owner) |
| P1.1 | `perf/p1-size` / #2 | [41426763606](https://github.com/MaxxingAll/PicoWallpaper/actions/runs/41426763606) | 583,168 → 403,456 bytes. Debug/Release CTest passed. Icon ID 1 retained. | Pending (owner) |
| P1.2 | `chore/pw-icon` / #3 | [41427942431](https://github.com/MaxxingAll/PicoWallpaper/actions/runs/41427942431) | Owner-supplied PW icon supersedes the recompressed icon from #2. Executable: 295,424 → 218,624 bytes; ICO: 109,649 → 32,453 bytes. Debug/Release CTest passed. | Scaling check pending (owner) |
| P1.3 | `perf/p1-size` / #2 | [41426834216](https://github.com/MaxxingAll/PicoWallpaper/actions/runs/41426834216) | 333,312 → 295,424 bytes. Debug/Release CTest passed, including formatter boundaries. Formatting routines removed; literal object-name check unmet (see below). | — |
| P1.4 | — | — | Dropped (D1: No) | — |
| P3.1 | | | | Pending (owner) |
| P3.2 | | | | Pending (owner) |
| P3.3 | | | | Pending (owner) |
| P4.1 | | | | Pending (owner) |
| P4.2 | — | — | Dropped (D2: No) | — |
| P4.3 | | | | Pending (owner) |
| P5.1 | | | | Pending (owner) |
| P5.2 | | | | — |

P1.3 investigation: the [Release linker trace](https://github.com/MaxxingAll/PicoWallpaper/actions/runs/41427216253)
reports 28 discarded `__stdio_common_v*` routines. The map no longer lists `snprintf`, `swprintf_s`,
`__stdio_common_vsprintf`, `__stdio_common_vswprintf_s` or `printf_count_output.obj`.
The [Release disassembly](https://github.com/MaxxingAll/PicoWallpaper/actions/runs/41427504985) shows
`_invalid_parameter`, `_fflush_nolock` and `fclose` calling `get_raw_ptd`, and `_mbtowc_internal` and
`_write_nolock` calling `update_locale_slow`. These are the two functions attributed to `libucrt:output.obj`.
CRT startup (`post_pgo_initialization`) also calls `__scrt_initialize_default_local_stdio_options`, which
calls `__local_stdio_printf_options`. The static CRT still uses these helpers after both app call sites are replaced.
