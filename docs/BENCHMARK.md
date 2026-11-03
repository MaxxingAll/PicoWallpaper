# PicoWallpaper Benchmark Method

This document defines the repeatable benchmark used to compare upstream v2.0.0
with PicoWallpaper. Run both builds on the same Windows machine,
with the same video, display configuration, power plan, and background workload.

## Reference workload

- Videos: 1920x1080, VP9 WebM, Opus audio, 8 seconds; use a 30 fps clip
  and the same clip re-encoded at 60 fps
- Playback: muted, seamless looping; use the 30 FPS cap for the 30 fps clip
  and the 60 FPS cap for the 60 fps clip
- Display: record resolution, refresh rate, scaling, and selected GPU
- Power: AC power, Windows power mode set to **Balanced**
- Sampling: wait until at least five loops complete

Record each test clip's filename and SHA-256 with every result. Do not commit a
copyrighted clip to the repository.

## Build configurations

Build upstream v2.0.0 using its documented vcpkg and FFmpeg setup. Build this
fork in Release mode without a package manager:

```powershell
cmake -B build -S . -A x64
cmake --build build --config Release
```

## Measurements

1. Reboot, sign in, and wait for background activity to settle.
2. Start Windows Performance Recorder or Task Manager before launching the app.
3. Launch the build and select the reference clip. In the fork, use tray →
   **Choose video...**, then set tray → **FPS cap** to match the clip.
4. Confirm **GPU > Video Decode** is active for the app's selected adapter.
5. After five loops, sample for 60 seconds and record median and peak values:
   - CPU percentage
   - private working set and private commit
   - dedicated and shared GPU memory
   - context switches per second as a wake-up proxy, using WPR CPU Usage
     (Precise) or Process Explorer's **Context Switch Delta** divided by the
     sampling interval in seconds
   - frame-time spread using WPR or PresentMon
6. Use tray → **Pause** in the fork. After playback suspends, record private
   working set, private commit, and context switches per second again. The fork
   trims its working set on suspend, so private commit is also needed.
7. Exit, clear standby effects by rebooting, then measure cold launch to first
   presented frame. Repeat three times and report the median.
8. Record the executable size and total uncompressed release size. Repeat the
   playback and pause measurements with the 60 fps reference clip and cap.

## Environment

| Item | Value |
|---|---|
| Date | Pending |
| Commit | Pending |
| Windows build | Pending |
| CPU | Pending |
| GPU and driver | Pending |
| RAM | Pending |
| Display | Pending |
| Codec extensions | Pending |
| 30 fps clip filename / SHA-256 | Pending (owner) |
| 60 fps clip filename / SHA-256 | Pending (owner) |

## Baseline results

| Metric | Upstream v2.0.0, 30 fps | PicoWallpaper, 30 fps | PicoWallpaper, 60 fps | Target |
|---|---:|---:|---:|---:|
| Executable size | Pending (owner) | Pending (owner) | Pending (owner) | <= 1.5 MB |
| Release size, uncompressed | Pending (owner) | Pending (owner) | Pending (owner) | <= 1.5 MB |
| CPU median / peak | Pending (owner) | Pending (owner) | Pending (owner) | <= 0.3% median |
| Private working set median / peak | Pending (owner) | Pending (owner) | Pending (owner) | <= 6 MB median |
| Paused private working set | Pending (owner) | Pending (owner) | Pending (owner) | < 1 MB |
| Paused private commit | Pending (owner) | Pending (owner) | Pending (owner) | Record only |
| Wake-ups per second while playing (context-switch proxy) | Pending (owner) | Pending (owner) | Pending (owner) | Record only |
| Wake-ups per second while paused (context-switch proxy) | Pending (owner) | Pending (owner) | Pending (owner) | Record only |
| Frame-time spread while playing | Pending (owner) | Pending (owner) | Pending (owner) | Record only |
| Dedicated/shared GPU memory | Pending (owner) | Pending (owner) | Pending (owner) | <= 50 MB total |
| Cold launch to first frame | Pending (owner) | Pending (owner) | Pending (owner) | Record only |

Raw WPR traces and screenshots should be attached to the corresponding GitHub
issue or release artifact; only the summarized results belong in this file.
