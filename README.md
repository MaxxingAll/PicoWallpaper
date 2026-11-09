# PicoWallpaper

PicoWallpaper is a small, native video-wallpaper player for Windows 10 and
Windows 11. It uses Windows Media Foundation for decoding and Direct3D 11 for
presentation, so releases contain one executable and no bundled FFmpeg or codec
DLLs.

This project is a GPL-3.0 fork of `f4rdani/LiteWallpaper`.

## Features

- Media Foundation hardware decoding into D3D11 NV12 surfaces
- Direct3D 11 video-processor presentation without a CPU frame copy
- WorkerW desktop integration
- A separate video for each monitor; a monitor without one keeps the Windows wallpaper
- Native system-tray controls; no web runtime or UI framework
- Automatic pause for fullscreen apps, maximized apps, lock, and optionally battery
- 15, 30, 60, or native FPS cap
- Plain HKCU `Run` autostart entry
- Loads only Microsoft-signed modules, so overlays and hooks (RTSS, Discord, Steam)
  stay out of the wallpaper; Windows, WHQL GPU drivers and Store codec extensions load normally
- No network access

## Usage

1. Run `PicoWallpaper.exe`.
2. Open its tray icon, pick **Monitor 1** (or another monitor) and choose **Video...**.
   **Remove** gives that monitor back to the Windows wallpaper.
3. Use **Settings** in the same menu to pause, select an FPS cap, configure pause
   behavior or autostart; **Exit** closes the app.

Monitor numbers are the Windows display numbers (Settings → System → Display → Identify).
A maximized window pauses only the monitor it is on. Fullscreen apps, battery,
lock and **Settings → Pause** pause all monitors; the FPS cap applies to all.

Settings are stored in `%APPDATA%\PicoWallpaper\config.ini`; each monitor's video
is listed under `[monitors]`. On first launch,
settings from the previous version are copied if no new configuration exists; the
old configuration remains intact. Its autostart entry is migrated to `PicoWallpaper`.
A video chosen in a single-video version moves to the main monitor.

Supported formats are the formats installed Windows can decode. H.264 MP4 is
built into standard Windows editions. VP9, AV1, HEVC, WebM, or MKV playback can
require the matching Windows codec extension. Third-party (non-Microsoft-signed)
Media Foundation codecs are not loaded.

Wallpapers are muted. To create a smaller 1080p H.264 copy outside the app:

```text
ffmpeg -i input.mp4 -vf scale=-2:1080 -an -c:v libx264 -crf 20 -preset medium output.mp4
```

## Build

Requirements:

- Windows 10 or 11
- Visual Studio 2022 with Desktop development with C++
- CMake 3.20 or newer

```powershell
cmake -B build -S . -A x64
cmake --build build --config Release
```

The result is `build/Release/PicoWallpaper.exe`. The release build uses the
static C runtime and imports only Windows system libraries.

## Uninstall

Exit the tray application and delete the executable. If autostart was enabled,
turn it off from the tray first or remove the `PicoWallpaper` value under
`HKCU\Software\Microsoft\Windows\CurrentVersion\Run`. The optional configuration
directory can then be deleted.

## License

GPL-3.0. See `LICENSE`.
