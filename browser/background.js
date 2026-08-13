// Audio Ducker - YouTube playback detector (background script)
// Aggregates the playing/not-playing state from content scripts across all
// YouTube tabs and reports the count to the native messaging host
// "com.audiodycker.youtube" whenever it changes (and periodically, so a
// freshly restarted Audio Ducker process quickly catches up).

"use strict";

const HOST = "com.audiodycker.youtube";
const playingTabs = new Set();

function sendNative(msg) {
  // If the app (and its host) is not running, lastError is set; ignore it.
  chrome.runtime.sendNativeMessage(HOST, msg, function () {
    void chrome.runtime.lastError;
  });
}

function report() {
  sendNative({ type: "youtube-count", count: playingTabs.size });
}

chrome.runtime.onMessage.addListener(function (msg, sender) {
  if (!msg || msg.type !== "yt-state") return;
  const tabId = sender.tab ? sender.tab.id : null;
  if (tabId == null) return;

  const wasPlaying = playingTabs.has(tabId);
  if (msg.playing && !wasPlaying) {
    playingTabs.add(tabId);
    report();
  } else if (!msg.playing && wasPlaying) {
    playingTabs.delete(tabId);
    report();
  }
});

chrome.tabs.onRemoved.addListener(function (tabId) {
  if (playingTabs.delete(tabId)) {
    report();
  }
});

// Safety net: re-send the current count in case the app restarted while we
// were already running (the host only launches when a message is sent).
setInterval(report, 15000);
