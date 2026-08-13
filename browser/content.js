// Audio Ducker - YouTube playback detector (content script)
// Reports whether this YouTube tab is currently playing audible audio.
// Only sends a message on state transitions, so the background page can
// maintain a running count of playing tabs.

(function () {
  "use strict";

  function videos() {
    return Array.from(document.querySelectorAll("video"));
  }

  function isPlaying(v) {
    return (
      !v.paused &&
      !v.ended &&
      !v.muted &&
      v.volume > 0 &&
      v.readyState >= 3 // HAVE_FUTURE_DATA: we know we have actual audio
    );
  }

  let playing = false;

  function check() {
    let any = false;
    for (const v of videos()) {
      if (isPlaying(v)) {
        any = true;
        break;
      }
    }
    if (any !== playing) {
      playing = any;
      chrome.runtime.sendMessage({ type: "yt-state", playing: playing });
    }
  }

  document.addEventListener("play", check, true);
  document.addEventListener("pause", check, true);
  document.addEventListener("ended", check, true);
  document.addEventListener("volumechange", check, true);

  // YouTube's SPA may swap or toggle video elements without reliable events,
  // so also watch the DOM and re-check on a short interval as a safety net.
  const observer = new MutationObserver(check);
  observer.observe(document.documentElement, { childList: true, subtree: true });
  setInterval(check, 2000);

  window.addEventListener("beforeunload", function () {
    if (playing) {
      chrome.runtime.sendMessage({ type: "yt-state", playing: false });
    }
  });

  check();
})();
