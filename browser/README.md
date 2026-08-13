# Browser extension (YouTube tab counting)

This optional extension makes YouTube detection precise: instead of guessing
from the browser's master audio, it counts exactly how many YouTube tabs are
playing audible audio and tells the app through a native messaging host
(`com.audiodycker.youtube`).

Without the extension, Audio Ducker still works, but it treats the whole
browser process as one ducking source whenever the browser produces any sound.

## Files

- `content.js` — shared content script injected into `*.youtube.com`. Detects
  when a video is actually playing audible audio (not paused, not muted, not
  ended, ready to decode) and reports state transitions to the background.
- `background.js` — shared background script. Aggregates the per-tab state,
  keeps a running count, and forwards `{"type":"youtube-count","count":N}` to
  the native messaging host. Re-sends the count every 15 s so a restarted
  Audio Ducker process catches up quickly.
- `chrome/manifest.json` — Chrome/Edge (Manifest V3).
- `firefox/manifest.json` — Firefox (Manifest V2; the fixed extension ID
  `audio-ducker@audiodycker.local` is what the host manifest allows).

## Install (Chrome / Edge)

1. Load the extension as unpacked:
   - chrome://extensions -> enable "Developer mode" -> "Load unpacked" ->
     select this `browser/` folder (the folder containing `chrome/`).
2. Note the extension ID shown on the card (e.g. `aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa`).
3. In Audio Ducker settings, enable the browser extension and paste that ID,
   then save. The app writes the native messaging host manifest under the
   user's Local AppData and registers it in the registry (HKCU).

## Install (Firefox)

1. about:debugging -> This Firefox -> Load Temporary Add-on -> select
   `browser/firefox/manifest.json` (temporary install; for a permanent install
   you would need a signed `.xpi`).
2. Enable the browser extension in Audio Ducker settings and save. The
   Firefox host manifest is written to `%LOCALAPPDATA%\...\Mozilla\NativeMessagingHosts`
   and the registry, already pinned to the `audio-ducker@audiodycker.local` ID.

## Verify

- Play a YouTube video in one tab: the app should duck other apps.
- Pause it: after the fade time, volumes restore.
- Open more YouTube tabs (playing): the count should rise, but that only
  matters while audio is actually audible; the app ducks regardless of the
  number.
