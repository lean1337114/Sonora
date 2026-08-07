# Sonora

Hand-drawn Win32 audio extractor in pure C. Paste a video link and get an MP3, or pick a local media file and convert it. The interface is drawn entirely by hand, with no dependencies beyond `yt-dlp.exe` and `ffmpeg.exe`.

## Requirements

**Build:** MinGW-w64, or any GCC that targets Windows.

**Runtime:** Windows 7 or later. You need two executables in the same folder as `Sonora.exe` (or somewhere on `PATH`):

- **ffmpeg** — download from https://www.ffmpeg.org/ and place `ffmpeg.exe` in the same folder as `Sonora.exe`.
- **yt-dlp** — required only for downloading from links. Place `yt-dlp.exe` in the same folder.

Recommended layout:

```
Sonora/
├── Sonora.exe
├── yt-dlp.exe
├── ffmpeg.exe
└── *.mp3          ← downloads land here
```

## Build

```sh
gcc sonora.c -o Sonora.exe -O2 -mwindows -lgdi32 -luser32 -lshell32 -lcomdlg32 -lm
```

`-mwindows` suppresses the console window. `-lm` is required by the rasterizer.

## Usage

**Download MP3.** Paste a URL into the first field (the *Paste* button pulls straight from the clipboard). Optionally type a name in the second field; leave it empty and the video title is used. Press the button or hit `Enter`. The MP3 is saved to the `Sonora.exe` folder; clicking the path in the footer opens that folder.

**Convert File.** Opens a picker filtered to common media formats. The MP3 is written next to the source file, not to the program folder.

**Cancel.** While a job runs, the primary button becomes *Cancel* and stops the operation. Partial files are left on disk.

Two indicators in the footer show whether `yt-dlp` and `ffmpeg` were found. The check runs once, at startup; if you add a missing executable while Sonora is open, restart it.

### Keyboard

| Key | Action |
|---|---|
| `Enter` | Trigger the primary button (download, or cancel while busy) |
| `Esc` | Close the window |
| `Tab` | Cycle between the text fields |

## Notice

Sonora is only a front end: it does not download or decode anything itself, it just invokes the tools you supply. Whether a given extraction is permitted depends on the source platform's terms of service and on the copyright status of the material.
