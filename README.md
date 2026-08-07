# Sonora

A single-file Win32 audio extractor. Paste a video link, get an MP3. Or pick a local media file and transcode it.

The entire interface is drawn by hand into a DIB section: no GDI+, no Direct2D, no XAML, no manifest, no resource file. The only third-party pieces are two external executables it shells out to, `yt-dlp.exe` and `ffmpeg.exe`.

```
sonora.c          ~1000 lines
Sonora.exe        one binary, links only against system DLLs
```

---

## Requirements

**Build:** MinGW-w64 (or any GCC targeting Windows). No headers beyond the Win32 SDK that ships with the toolchain.

**Runtime:** Windows 7 or later. `yt-dlp.exe` and `ffmpeg.exe` must be reachable, either in the program folder or anywhere on `PATH`. Neither is bundled; download them separately.

## Build

```sh
gcc sonora.c -o Sonora.exe -O2 -mwindows -lgdi32 -luser32 -lshell32 -lcomdlg32 -lm
```

`-mwindows` suppresses the console window. `-lm` is required by the rasterizer (`sqrtf`, `expf`, `fmodf`).

The file also compiles as a component of a larger program: `WinMain` is guarded at the bottom and simply calls the exported entry point, so linking `sonora.c` alongside another translation unit that defines its own `main` works as long as you drop the `WinMain` block.

## Recommended folder layout

```
Sonora/
├── Sonora.exe
├── yt-dlp.exe
├── ffmpeg.exe
└── *.mp3          ← downloads land here
```

Downloads are written to the directory containing `Sonora.exe`. Clicking the path in the footer opens that folder in Explorer.

---

## Usage

**Download MP3.** Paste a URL into the first field (the *Paste* pill pulls from the clipboard in one click). Optionally type a filename in the second field; leave it empty and the video title is used instead. Press the button or hit `Enter`.

**Convert File.** Opens a file picker filtered to common media containers. The MP3 is written next to the source file, not to the program folder. This asymmetry is deliberate but easy to forget.

**Cancel.** While a job runs, the primary button becomes *Cancel* and terminates the child process. Partial output is left on disk; nothing is cleaned up.

Two indicator pills in the footer show whether `yt-dlp` and `ffmpeg` were found. They are probed once at startup, so dropping a missing executable into the folder while Sonora is open will not update them until restart.

### Keyboard

| Key | Action |
|---|---|
| `Enter` | Trigger the primary button (download, or cancel while busy) |
| `Esc` | Close the window |
| `Tab` | Cycle the two text fields |

The title bar is fake but draggable; the window is a `WS_POPUP` with a rounded region and a drop shadow.

---

## Commands issued

Sonora builds these command lines and captures their combined stdout/stderr through an anonymous pipe.

**Download:**

```
yt-dlp --newline --no-playlist --no-warnings --ignore-config
       -x --audio-format mp3 --audio-quality 0
       -o "<outdir>\<name>.%(ext)s" -- "<url>"
```

`--audio-quality 0` requests the best VBR LAME setting. `--ignore-config` means a user's global `yt-dlp.conf` is intentionally ignored, so behavior stays predictable. `--` terminates option parsing before the URL.

**Convert:**

```
ffmpeg -hide_banner -y -i "<file>" -vn -c:a libmp3lame -q:a 2 "<file>.mp3"
```

`-q:a 2` is roughly V2, around 190 kbps average. `-y` overwrites without asking.

---

## Architecture

**Rasterizer.** `rrFill`, `rrStroke`, `line`, `tri`, and `glow` write straight into a 32-bit BGRA buffer. Rounded rectangles and line segments use signed distance fields, sampled once per pixel and clamped to produce coverage; triangles use a 3×3 supersample grid instead, since an SDF for arbitrary triangles is not worth the code. `blendPx` does fixed-point source-over compositing. Vertical gradients are computed per scanline, so a full-height gradient costs one `mixCol` per row rather than one per pixel.

**Text** is the one thing not hand-rolled. `DrawTextW` renders onto the same DIB through `A.memDC` after the shapes are laid down, which keeps ClearType working. `GdiFlush` before the final `BitBlt` is mandatory here: without it, GDI's batched text can land after the blit.

**Layout** is fully recomputed in `Layout()` from a single DPI value through the `S()` macro. `MakeFonts` and `PlaceChildren` follow. `WM_DPICHANGED` re-runs all three and discards the back buffer.

**Jobs** run on a worker thread. The child process writes to a pipe; the worker accumulates bytes into lines and feeds each to `parseLine`, which recognizes yt-dlp's `[download] NN.N%` markers and ffmpeg's `Duration:` / `time=` pairs to derive a fraction. Progress and status text cross back to the UI thread as `WM_APP_PROG` and `WM_APP_TEXT` posted messages; the status string is heap-allocated by the worker and freed by the window procedure. A critical section guards the single `g_child` handle so `CancelJob` cannot terminate a stale process.

**Animation** is a 16 ms timer driving exponential interpolation on hover states and the progress bar, plus a free-running `phase` used by the shimmer sweep and the status-dot pulse. `Animate()` returns whether anything moved, so the window stops repainting when idle.

**The window icon** is generated at runtime by `MakeIcon`, which renders `drawMark` into a 64×64 DIB and wraps it in an `ICONINFO`. No `.ico` file, no resource compiler step.

---

## Tuning points

Everything visual lives in the token block near the top of the file. Colors are `0xRRGGBB` literals consumed by `mixCol` and `blendPx` directly.

| Constant | Role |
|---|---|
| `C_BG_TOP` / `C_BG_BOT` | Window gradient endpoints |
| `C_ACC1` / `C_ACC2` | Primary button, progress fill, app mark |
| `C_GLOW_A` / `C_GLOW_B` | The two corner bloom sources |
| `C_OK` / `C_ERR` | Status dot and dependency pills |

Window dimensions are the `S(560)` and `S(384)` in `Layout()`; every rectangle below them is positioned absolutely, so changing the size means adjusting the rows by hand.

---

## Known limitations

The initial DPI is hardcoded to 96 in the entry point rather than queried with `GetDpiForWindow` or `GetDpiForMonitor`. Since per-monitor v2 awareness is enabled, a window opened on a 150% display renders at 100% scale until it receives a `WM_DPICHANGED`, typically by being dragged to another monitor and back. This is the most visible rough edge and worth fixing first.

Beyond that:

- Output directory is fixed to the executable's folder and cannot be changed from the UI.
- `--no-playlist` is hardcoded, so playlist URLs yield a single track.
- Cancelling leaves partial files behind.
- Dependency detection runs once, at startup.
- `parseLine` matches yt-dlp's human-readable output. A future format change breaks progress reporting silently; the download itself still works.
- No retry, no queue, no history. One job at a time.

## Troubleshooting

| Symptom | Cause |
|---|---|
| "Place yt-dlp.exe in the program folder." | Executable not in the app directory or on `PATH` |
| "yt-dlp rejected this link." | Unsupported site, private video, or region block |
| Download completes but no MP3 appears | `ffmpeg` missing; yt-dlp downloaded the stream but could not transcode |
| Progress bar stays at 0% but the file appears | yt-dlp output format changed; parsing failed, download did not |
| UI looks small and blurry on a 4K display | The initial-DPI limitation above |

---

## Legal

Sonora is a front end. It does not download or decode anything itself; it invokes tools you supply. Whether a given extraction is permitted depends on the source platform's terms of service and on the copyright status of the material. That determination is yours.

## License

Not yet specified. Add one before distributing, particularly since the runtime dependencies carry their own terms: yt-dlp is Unlicense, ffmpeg is LGPL or GPL depending on how the build was configured.
