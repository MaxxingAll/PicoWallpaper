# Review validation

## CI evidence

Initial code commit `9af78c7` passed the Windows Debug and Release builds and
CTest (1/1 regression executable in each configuration) in
[run 41418443319](https://github.com/MaxxingAll/PicoWallpaper/actions/runs/41418443319).
Release executable: **582144 bytes**, below the **1572864-byte** budget.
Ten existing C4245 decoder stream-enum conversion warnings per configuration
remain open in FIXTODO.

Follow-up regression coverage adds full-frame fallback for unusable optional
apertures and driver color-conversion support/fallback queries. These tests use
media types and a COM enumerator stub; they do not validate rendered pixels or
real GPU driver support. Follow-up CI is pending.

## Pending hardware checks

- GPU reset during pause/resume and successful recovery with a valid video.
- Moved/deleted or undecodable video after GPU recovery: no endless device
  rebuilds; selecting another video remains immediately available.
- Padded/offset/fractional apertures and malformed aperture fallback in real clips.
- Full/studio-range video and tagged matrices, including a driver that rejects
  the requested conversion and accepts the limited-range BT.709 fallback.
- Missing-codec/corrupt-file pause cycles: one error, no automatic retry of the
  failed file until it is explicitly selected again.
- Explorer restart, monitor changes, fullscreen/maximized, battery, lock and sleep.
- H.264/VP9 looping, Video Decode activity, system-only imports and benchmarks
  described in `BENCHMARK.md`.
