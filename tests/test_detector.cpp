#include "TestHarness.h"

#include "detection/BrowserActivityDetector.h"

using ducker::BrowserActivityDetector;

TEST(BrowserDetectorIgnoresNonBrowser) {
    BrowserActivityDetector d;
    d.SetEnabledBrowsers({"chrome.exe", "msedge.exe"});
    d.OnSessionAdded("s1", "vlc.exe", true, false);
    EXPECT_EQ(d.Count(), 0);
}

TEST(BrowserDetectorNameMatchingIsCaseInsensitive) {
    BrowserActivityDetector d;
    d.SetEnabledBrowsers({"chrome.exe"});
    EXPECT_TRUE(d.IsBrowser("chrome.exe"));
    EXPECT_TRUE(d.IsBrowser("CHROME.EXE"));
    EXPECT_TRUE(d.IsBrowser("Chrome.exe"));
    EXPECT_FALSE(d.IsBrowser("firefox.exe"));
    EXPECT_FALSE(d.IsBrowser("chrome"));
}

TEST(BrowserDetectorCountsActiveUnmuted) {
    BrowserActivityDetector d;
    d.SetEnabledBrowsers({"chrome.exe"});

    d.OnSessionAdded("s1", "chrome.exe", true, false);
    EXPECT_EQ(d.Count(), 1);

    // A second playing tab counts too.
    d.OnSessionAdded("s2", "chrome.exe", true, false);
    EXPECT_EQ(d.Count(), 2);

    // Muted tab: no longer duck-worthy.
    d.OnSessionMuted("s1", true);
    EXPECT_EQ(d.Count(), 1);

    // Paused tab: no audio.
    d.OnSessionState("s2", false);
    EXPECT_EQ(d.Count(), 0);

    // Unmute and resume: both sessions are duck-worthy again.
    d.OnSessionMuted("s1", false);
    d.OnSessionState("s2", true);
    EXPECT_EQ(d.Count(), 2);
}

TEST(BrowserDetectorMutedOnAdd) {
    BrowserActivityDetector d;
    d.SetEnabledBrowsers({"chrome.exe"});
    d.OnSessionAdded("s1", "chrome.exe", true, true);
    EXPECT_EQ(d.Count(), 0);
    d.OnSessionMuted("s1", false);
    EXPECT_EQ(d.Count(), 1);
}

TEST(BrowserDetectorRemoveAndReset) {
    BrowserActivityDetector d;
    d.SetEnabledBrowsers({"chrome.exe"});
    d.OnSessionAdded("s1", "chrome.exe", true, false);
    d.OnSessionAdded("s2", "chrome.exe", true, false);
    EXPECT_EQ(d.Count(), 2);

    d.OnSessionRemoved("s1");
    EXPECT_EQ(d.Count(), 1);

    d.Reset();
    EXPECT_EQ(d.Count(), 0);
    // Removals after a reset are no-ops.
    d.OnSessionRemoved("s2");
    EXPECT_EQ(d.Count(), 0);
}

TEST(BrowserDetectorStateOpsAreNoopForUnknownSessions) {
    BrowserActivityDetector d;
    d.SetEnabledBrowsers({"chrome.exe"});
    d.OnSessionState("missing", true);
    d.OnSessionMuted("missing", true);
    d.OnSessionRemoved("missing");
    EXPECT_EQ(d.Count(), 0);
}

TEST(BrowserDetectorCallbackFiresOnCountChangeOnly) {
    BrowserActivityDetector d;
    d.SetEnabledBrowsers({"chrome.exe"});
    int calls = 0;
    int last = -1;
    d.SetCallback([&](int c) {
        calls++;
        last = c;
    });

    d.OnSessionAdded("s1", "chrome.exe", true, false); // 1
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(last, 1);
    d.OnSessionAdded("s2", "chrome.exe", true, false); // 2
    EXPECT_EQ(calls, 2);
    d.OnSessionMuted("s2", true); // 1
    EXPECT_EQ(calls, 3);
    d.OnSessionMuted("s2", true); // unchanged -> no callback
    EXPECT_EQ(calls, 3);
    d.OnSessionState("s1", false); // 0
    EXPECT_EQ(calls, 4);
    EXPECT_EQ(last, 0);
    d.Reset(); // 0, unchanged -> no callback
    EXPECT_EQ(calls, 4);
}
