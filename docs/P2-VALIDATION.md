# Review fix candidate, 2026-11-02

Base: main `f0337c8`. Scope: the six findings from the main-branch review.
The previous session's partial changes are preserved in this candidate.

## CI result

Code commit `9af78c7` on `fix/p2-review` passed the Windows Debug and Release
builds and CTest (1/1 regression executable in each configuration) in
[run 41418443319](https://github.com/MaxxingAll/PicoWallpaper/actions/runs/41418443319).
Release executable size is **582144 bytes**, below the **1572864-byte** budget.
The existing ten C4245 decoder stream-enum conversion warnings appear in both
configurations; that FIXTODO cleanup remains open. These tests do not exercise
live decoding, rendered pixels, device loss or desktop/shell recovery.

| Finding | Candidate behavior | Verification |
|---|---|---|
| P1: lost device during pause | Check device health before opening, after failed open and on the recovery timer, including while suspended. Recovery attempts remain retryable. | Source tracing; live reset/resume test pending. |
| P2: texture padding | Carry frame dimensions, visible aperture and pixel aspect ratio to the presenter. Compute cover/fit/stretch from the aperture, retaining texture dimensions only for GPU allocation checks. | Debug/Release CTest passed: 1920x1080 in 1920x1088, offset apertures, fractional offsets, malformed metadata and 4:3 non-square pixels. Rendered checks pending. |
| P2: full-range/matrix conversion | Read range and matrix from the current output media type; set explicit D3D11 input and full-range RGB output color spaces. Use the SD/HD heuristic only when matrix metadata is missing. | Debug/Release CTest passed: full/studio range, BT.601, BT.709, BT.2020 and BGRA. Rendered color checks pending. |
| P2: repeated decode dialogs | Remember failed file paths, block automatic reopen, and clear the failure only after success or deliberate video selection. Device-loss failures stay on the recovery path. | Source tracing; missing-codec/corrupt-file pause cycles pending. |
| P2: Debug build flags | Restrict optimization and LTCG flags to Release. | Debug and Release CI builds and CTest passed. |
| P2: deleted Run entry | Disable the saved flag if the Run value was removed; repair the command if the executable moved; preserve the setting on registry errors. | Process-isolated registry tests passed in Debug and Release. |

## Geometry contract

The parser uses enabled pan-scan, minimum display aperture, geometric aperture,
then frame size in that order. It validates the complete aperture blob and its
bounds. Fractional source edges round inward to the integer D3D11 rectangle;
this can discard less than one pixel at each edge but never includes pixels
outside the valid aperture. Frame metadata refreshes on media-type changes.

References: [Microsoft display-area and aspect-ratio guidance](https://learn.microsoft.com/en-us/windows/win32/medfound/picture-aspect-ratio),
[minimum display aperture](https://learn.microsoft.com/en-us/windows/win32/medfound/mf-mt-minimum-display-aperture-attribute),
[explicit D3D11 color spaces](https://learn.microsoft.com/en-us/windows/win32/api/d3d11_1/nn-d3d11_1-id3d11videocontext1).
HDR/tone mapping remains outside this review fix.

## Preparation environment and remaining validation

- The initial restricted session could not write the original `C:\Work\LiteWallpaper-MF` checkout.
  Candidate work is in `C:\Users\Windows11\AppData\Local\Temp\litewallpaper-p2\repo`.
- The shared-repo pull failed while opening `.git/FETCH_HEAD` for writing.
  No shared instruction or memory files were changed.
- No MSVC/Clang/GCC toolchain, CMake or Windows SDK headers were found locally.
  Builds, CTest and the size measurement were performed by Windows CI, as linked
  above. No local C++ build or hardware test is claimed.
- `git diff --check` passed for the complete candidate, including new source and
  test files. This checks patch whitespace; it does not validate C++ behavior.
- Initial GitHub access failed with `WinError 10061` before authentication.
  Access was subsequently restored. The repository was renamed to
  `MaxxingAll/PicoWallpaper`; the old installation endpoint returned 404.
  Authentication as `gpt-chan` succeeds against the new repository, and its
  main branch is still `f0337c8`. The candidate remote now uses the new name.
- Do not replace the original checkout wholesale: it contains uncommitted work.
  Review the candidate diff against `f0337c8` and compare any later source changes
  before applying it. All commits/pushes must use the configured `gpt-chan[bot]`.

Next: review/merge the branch, then run GPU-reset/resume, corrupt-video retry and
rendered padding/color checks. Benchmarks and a release tag are separate pending
work.
