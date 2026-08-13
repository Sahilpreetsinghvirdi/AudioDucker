#include "TestHarness.h"

#include <windows.h>

#include <string>

#include "config/ConfigManager.h"

using ducker::AppSettings;
using ducker::ConfigManager;

namespace {

std::wstring TempConfigPath(const wchar_t* tag) {
    wchar_t dir[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, dir);
    std::wstring path = dir;
    path += L"audiodycker_test_";
    path += tag;
    path += L"_";
    path += std::to_wstring(GetCurrentProcessId());
    path += L".ini";
    return path;
}

void Cleanup(const std::wstring& path) { DeleteFileW(path.c_str()); }

} // namespace

TEST(ConfigDefaultsWhenMissing) {
    std::wstring path = TempConfigPath(L"defaults");
    Cleanup(path);

    ConfigManager cm(path);
    cm.Load();
    AppSettings s = cm.Get();

    EXPECT_NEAR(s.duckVolume, 0.25f, 1e-6);
    EXPECT_EQ(s.fadeDownMs, 500);
    EXPECT_EQ(s.fadeUpMs, 700);
    EXPECT_TRUE(s.browserChrome);
    EXPECT_TRUE(s.browserEdge);
    EXPECT_TRUE(s.browserFirefox);
    EXPECT_TRUE(s.useAudioDetection);
    EXPECT_FALSE(s.duckAllOthers);
    EXPECT_FALSE(s.startWithWindows);
    EXPECT_FALSE(s.verboseLogging);
    EXPECT_EQ(s.extensionId, "");

    // A sensible default app list comes pre-enabled.
    EXPECT_FALSE(s.enabledApps.empty());
    EXPECT_TRUE(s.enabledApps.count("spotify.exe") != 0);
    EXPECT_TRUE(s.enabledApps.count("vlc.exe") != 0);

    Cleanup(path);
}

TEST(ConfigRoundTrip) {
    std::wstring path = TempConfigPath(L"roundtrip");
    Cleanup(path);

    {
        ConfigManager cm(path);
        cm.Load();
        AppSettings s = cm.Get();
        s.duckVolume = 0.40f;
        s.fadeDownMs = 250;
        s.fadeUpMs = 1000;
        s.browserChrome = false;
        s.browserEdge = true;
        s.browserFirefox = false;
        s.useAudioDetection = true;
        s.duckAllOthers = true;
        s.verboseLogging = true;
        s.startWithWindows = false; // avoid touching the user's Run key
        s.extensionId = "aabbccddee";
        s.enabledApps.clear();
        s.enabledApps.insert("spotify.exe");
        s.enabledApps.insert("mpv.exe");
        cm.Set(s);
    }

    {
        ConfigManager cm2(path);
        cm2.Load();
        AppSettings s = cm2.Get();

        EXPECT_NEAR(s.duckVolume, 0.40f, 1e-6);
        EXPECT_EQ(s.fadeDownMs, 250);
        EXPECT_EQ(s.fadeUpMs, 1000);
        EXPECT_FALSE(s.browserChrome);
        EXPECT_TRUE(s.browserEdge);
        EXPECT_FALSE(s.browserFirefox);
        EXPECT_TRUE(s.useAudioDetection);
        EXPECT_TRUE(s.duckAllOthers);
        EXPECT_TRUE(s.verboseLogging);
        EXPECT_EQ(s.extensionId, "aabbccddee");
        EXPECT_EQ(s.enabledApps.size(), static_cast<size_t>(2));
        EXPECT_TRUE(s.enabledApps.count("spotify.exe") != 0);
        EXPECT_TRUE(s.enabledApps.count("mpv.exe") != 0);
        EXPECT_TRUE(s.enabledApps.count("vlc.exe") == 0);
    }

    Cleanup(path);
}

TEST(ConfigBrowserHelpers) {
    AppSettings s;
    auto names = s.GetEnabledBrowserNames();
    EXPECT_EQ(names.size(), static_cast<size_t>(3));
    EXPECT_TRUE(s.IsBrowserEnabled("chrome.exe"));
    EXPECT_TRUE(s.IsBrowserEnabled("msedge.exe"));
    EXPECT_TRUE(s.IsBrowserEnabled("firefox.exe"));

    s.browserChrome = false;
    s.browserFirefox = false;
    names = s.GetEnabledBrowserNames();
    EXPECT_EQ(names.size(), static_cast<size_t>(1));
    EXPECT_EQ(names[0], "msedge.exe");
    EXPECT_FALSE(s.IsBrowserEnabled("chrome.exe"));
    EXPECT_FALSE(s.IsBrowserEnabled("firefox.exe"));
    EXPECT_FALSE(s.IsBrowserEnabled("vlc.exe"));
}
