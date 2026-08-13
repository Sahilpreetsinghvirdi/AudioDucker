# Audio Ducker

A Windows 10/11 tray application that automatically **ducks** (lowers) the
volume of background applications while you are listening to audio in the
browser — typically YouTube — and smoothly **restores** them afterwards.

- Lowers the volume of selectable applications (e.g. Spotify, VLC) whenever
  the browser is producing sound.
- Fades volume down and back up smoothly (configurable fade time).
- Duck depth (target volume) and fade duration are configurable.
- Trays "duck all" / "stop" shortcuts and a settings window.
- Never touches the system volume or per-application volume of non-ducks.
- Detects browser audio precisely with the optional browser extension
  (native messaging); without it, falls back to the browser's audio session.
- Logs to a file in the user's Local AppData for diagnostics.

## Install

The quickest way to install and start Audio Ducker is to paste this into
Windows PowerShell (or any terminal):

```powershell
powershell -ExecutionPolicy Bypass -Command "irm https://raw.githubusercontent.com/Sahilpreetsinghvirdi/AudioDucker/main/install.ps1 | iex"
```

This downloads the latest release, installs it to
`%LOCALAPPDATA%\Programs\AudioDucker` and launches it. A speaker icon appears in
the system tray — right-click it to open Settings.

Alternatively, grab the `AudioDucker-windows-x64.zip` from the
[Releases](https://github.com/Sahilpreetsinghvirdi/AudioDucker/releases) page,
extract it anywhere and run `AudioDucker.exe`. The C runtime is linked in
statically, so no Visual C++ redistributable is needed.

## Features

- Audio session enumeration via the Windows Core Audio API (`IAudioSessionManager2`).
- Duck volume, fade time, and enabled applications configured in
  `%APPDATA%\AudioDucker\config.ini`.
- "User intervened" handling: if you raise an app's volume manually while it
  is ducked, that volume becomes the new restore baseline and the duck stops
  fighting you.
- Quiet apps are never raised above your baseline volume, and ducks never
  raise volume (fade only downward).
- Optional "forced duck" from the tray menu ducks all configured apps now.

## Build

Requirements: CMake >= 3.20, a recent MSVC toolchain with the Windows 10 SDK
(>= 10.0.19041; the project uses features from the 10.0.26100 SDK), 64-bit.

> **OneDrive note:** if the source tree lives inside a OneDrive-synced folder,
> Windows may block creating new files in it (OneDrive Files-On-Demand). The
> project therefore requires **out-of-source builds** (enforced by
> CMakeLists.txt) and generates the icon/resource files into the build
> directory. On a normal (non-OneDrive) drive you can just use `build/`; on
> OneDrive pick a directory outside the source tree, e.g.:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release
```

(Adjust the generator name to your installed Visual Studio; "Visual Studio 18
2026" and "Visual Studio 17 2022" are the VS2026/VS2022 generators.)

Outputs land in `<build-dir>\bin\<Config>\`:

- `AudioDucker.exe` — the tray app.
- `AudioDuckerHost.exe` — the native messaging host for the browser extension
  (must sit next to `AudioDucker.exe`; the app checks for it when you enable
  the extension in settings).
- `audio_ducker_tests.exe` — the test suite (66 tests; built with
  `-DAUDIO_DUCKER_BUILD_TESTS=OFF` to skip).

The C runtime is linked statically, so the exes run without the VC++
redistributable.

## Usage

1. Run `AudioDucker.exe` — an icon appears in the system tray.
2. Open Settings (right-click the tray icon -> Settings).
   - Enable the applications you want ducked (Spotify, VLC, game launchers...).
   - Set the duck level (target volume while ducking) and the fade time.
   - Optionally configure the browser extension (see below).
3. Play something in the browser. Configured apps fade down; when you pause,
   they fade back to their original volumes.

## Browser extension (precise YouTube detection)

Without the extension, the app listens to the browser's audio session: it
ducks whenever the browser makes *any* sound. The extension makes it precise —
it tells the app how many YouTube tabs are playing audible audio.

1. Install the extension: see [`browser/README.md`](browser/README.md)
   (Chrome/Edge unpacked; Firefox temporary add-on). The extension's
   background script reports the playing-tab count to
   `AudioDuckerHost.exe` via native messaging (`com.audiodycker.youtube`).
2. In Audio Ducker settings, check "Use browser extension", paste the
   Chrome/Edge extension ID, and Save. The app writes the host manifests
   (`com.audiodycker.youtube.chrome.json` / `.firefox.json`) and registers
   them in the registry (HKCU) and, for Firefox, in
   `%LOCALAPPDATA%\<...>\Mozilla\NativeMessagingHosts`.

Firefox uses the fixed add-on ID `audio-ducker@audiodycker.local`; no ID
entry is needed for Firefox.

## Configuration file

`%APPDATA%\AudioDucker\config.ini` — created with defaults on first run.

```ini
[general]
duckVolume=25
fadeDownMs=500
fadeUpMs=700
useAudioDetection=1
duckAllOthers=0
startWithWindows=0
verboseLogging=0
showNotifications=1
extensionId=[browsers]
chrome=1
edge=1
firefox=1
[apps]
mpv.exe=1
spotify.exe=1
vlc.exe=0
```

## How it works

1. `AudioSessionManager` enumerates audio sessions with `IAudioSessionManager2`,
   watches for session/device changes and volume changes.
2. `DuckingManager` decides when to duck: a "duck source" is the browser
   audio session and/or the browser-extension count (whichever is higher).
   When a source is active, configured apps are faded to the duck level;
   when it stops, they fade back to their saved baseline.
3. `VolumeFader` animates `ISimpleAudioVolume::SetMasterVolume` in the app's
   timer loop; `DuckingStateMachine` (unit-tested) tracks duck/restore
   transitions and user intervention.
4. The extension path: `browser/background.js` -> native message
   `{"type":"youtube-count","count":N}` -> `AudioDuckerHost.exe` ->
   `WM_COPYDATA` to the app's hidden main window.

## Logging

Diagnostics are appended to `%LOCALAPPDATA%\AudioDucker\audio-ducker.log`.
Use the tray menu to toggle more verbose logging (session lifecycle, volume
changes, duck/restore transitions).

## License

Copyright 2026 Sahil Vird. Licensed under the Apache License, Version 2.0.
See [LICENSE](LICENSE) for the full text.
