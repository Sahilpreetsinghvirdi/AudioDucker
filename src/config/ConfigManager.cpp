#include "config/ConfigManager.h"

#include "common/Helpers.h"
#include "common/JsonMini.h"
#include "core/VolumeSnapshot.h"

#include <cstdlib>

namespace ducker {

namespace {

const wchar_t* kSectionGeneral = L"general";
const wchar_t* kSectionBrowsers = L"browsers";
const wchar_t* kSectionApps = L"apps";

const wchar_t* kKeyDuckVolume = L"duckVolume";
const wchar_t* kKeyFadeDown = L"fadeDownMs";
const wchar_t* kKeyFadeUp = L"fadeUpMs";
const wchar_t* kKeyUseAudio = L"useAudioDetection";
const wchar_t* kKeyDuckAll = L"duckAllOthers";
const wchar_t* kKeyStartup = L"startWithWindows";
const wchar_t* kKeyVerbose = L"verboseLogging";
const wchar_t* kKeyExtId = L"extensionId";

// Sensible default set of media applications to duck out of the box.
const char* kDefaultApps[] = {
    "spotify.exe", "vlc.exe", "wmplayer.exe", "microsoft.media.player.exe",
    "musicbee.exe", "foobar2000.exe",
    "itunes.exe", "winamp.exe", "groovemusic.exe", "mediamonkey.exe",
    "potplayer.exe", "mpv.exe", "kodi.exe", "audacious.exe",
};

int ClampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

} // namespace

std::vector<std::string> AppSettings::GetEnabledBrowserNames() const {
    std::vector<std::string> names;
    if (browserChrome) names.push_back("chrome.exe");
    if (browserEdge) names.push_back("msedge.exe");
    if (browserFirefox) names.push_back("firefox.exe");
    return names;
}

bool AppSettings::IsBrowserEnabled(const std::string& lowerName) const {
    return (browserChrome && lowerName == "chrome.exe") ||
           (browserEdge && lowerName == "msedge.exe") ||
           (browserFirefox && lowerName == "firefox.exe");
}

ConfigManager::ConfigManager(std::wstring path) : path_(std::move(path)) {}

bool ConfigManager::Load() {
    IniFile ini(path_);
    ini.Load();

    auto readInt = [&](const wchar_t* key, int def) {
        std::wstring v = ini.Get(kSectionGeneral, key, L"");
        if (v.empty()) return def;
        return _wtoi(v.c_str());
    };
    auto readBool = [&](const wchar_t* key, bool def) {
        std::wstring v = ini.Get(kSectionGeneral, key, L"");
        if (v.empty()) return def;
        return v == L"1";
    };

    AppSettings s;
    s.duckVolume = vol::Clamp01(ClampInt(readInt(kKeyDuckVolume, 25), 1, 95) / 100.0f);
    s.fadeDownMs = ClampInt(readInt(kKeyFadeDown, 500), 0, 5000);
    s.fadeUpMs = ClampInt(readInt(kKeyFadeUp, 700), 0, 5000);
    s.useAudioDetection = readBool(kKeyUseAudio, true);
    s.duckAllOthers = readBool(kKeyDuckAll, false);
    s.startWithWindows = readBool(kKeyStartup, false);
    s.verboseLogging = readBool(kKeyVerbose, false);
    s.extensionId = WideToUtf8(ini.Get(kSectionGeneral, kKeyExtId, L""));

    s.browserChrome = ini.Get(kSectionBrowsers, L"chrome", L"1") == L"1";
    s.browserEdge = ini.Get(kSectionBrowsers, L"edge", L"1") == L"1";
    s.browserFirefox = ini.Get(kSectionBrowsers, L"firefox", L"1") == L"1";

    s.enabledApps.clear();
    auto keys = ini.Keys(kSectionApps);
    if (keys.empty()) {
        for (const char* app : kDefaultApps) s.enabledApps.insert(app);
    } else {
        for (const auto& [key, val] : keys) {
            if (val == L"1") s.enabledApps.insert(LowerAscii(WideToUtf8(key)));
        }
    }

    {
        std::lock_guard<std::mutex> lock(mu_);
        settings_ = std::move(s);
    }
    return true;
}

bool ConfigManager::Save() const {
    AppSettings s;
    {
        std::lock_guard<std::mutex> lock(mu_);
        s = settings_;
    }

    IniFile ini(path_);
    ini.Load();

    ini.Set(kSectionGeneral, kKeyDuckVolume, Utf8ToWide(std::to_string(static_cast<int>(s.duckVolume * 100.0f + 0.5f))));
    ini.Set(kSectionGeneral, kKeyFadeDown, Utf8ToWide(std::to_string(s.fadeDownMs)));
    ini.Set(kSectionGeneral, kKeyFadeUp, Utf8ToWide(std::to_string(s.fadeUpMs)));
    ini.Set(kSectionGeneral, kKeyUseAudio, s.useAudioDetection ? L"1" : L"0");
    ini.Set(kSectionGeneral, kKeyDuckAll, s.duckAllOthers ? L"1" : L"0");
    ini.Set(kSectionGeneral, kKeyStartup, s.startWithWindows ? L"1" : L"0");
    ini.Set(kSectionGeneral, kKeyVerbose, s.verboseLogging ? L"1" : L"0");
    ini.Set(kSectionGeneral, kKeyExtId, Utf8ToWide(s.extensionId));

    ini.Set(kSectionBrowsers, L"chrome", s.browserChrome ? L"1" : L"0");
    ini.Set(kSectionBrowsers, L"edge", s.browserEdge ? L"1" : L"0");
    ini.Set(kSectionBrowsers, L"firefox", s.browserFirefox ? L"1" : L"0");

    // Apps: write all known names, 1 for enabled.
    for (const auto& app : s.enabledApps)
        ini.Set(kSectionApps, Utf8ToWide(app), L"1");

    // Apply the "start with Windows" option.
    SetRunAtStartup(s.startWithWindows, ExePathW());
    return true;
}

AppSettings ConfigManager::Get() const {
    std::lock_guard<std::mutex> lock(mu_);
    return settings_;
}

void ConfigManager::Set(const AppSettings& settings) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        settings_ = settings;
    }
    Save();
}

// ---------------------------------------------------------------------------
// Native messaging host registration
// ---------------------------------------------------------------------------

HostRegistrationResult RegisterNativeMessagingHosts(const std::string& extensionId) {
    HostRegistrationResult result;

    std::wstring hostExe = ExePathW();
    size_t slash = hostExe.find_last_of(L"/\\");
    if (slash != std::wstring::npos)
        hostExe = hostExe.substr(0, slash) + L"\\AudioDuckerHost.exe";
    if (GetFileAttributesW(hostExe.c_str()) == INVALID_FILE_ATTRIBUTES) {
        result.ok = false;
        result.message = L"AudioDuckerHost.exe not found next to AudioDucker.exe.";
        return result;
    }

    std::wstring dir = GetHostManifestDirW();
    EnsureDir(dir);
    std::wstring chromeManifest = dir + L"\\com.audiodycker.youtube.chrome.json";
    std::wstring firefoxManifest = dir + L"\\com.audiodycker.youtube.firefox.json";

    bool haveExtId = !extensionId.empty();

    auto writeFile = [](const std::wstring& path, const std::string& data) {
        HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        DWORD written = 0;
        WriteFile(h, data.data(), (DWORD)data.size(), &written, nullptr);
        CloseHandle(h);
        return true;
    };

    std::string hostPathUtf8 = WideToUtf8(hostExe);
    int registrations = 0;
    std::wstring details;

    if (haveExtId) {
        std::string manifest =
            "{\n  \"name\": \"com.audiodycker.youtube\",\n"
            "  \"description\": \"Audio Ducker YouTube playback detector\",\n"
            "  \"path\": \"" + EscapeJsonString(hostPathUtf8) + "\",\n"
            "  \"type\": \"stdio\",\n"
            "  \"allowed_origins\": [\"chrome-extension://" + extensionId + "/\"]\n}\n";
        if (!writeFile(chromeManifest, manifest)) {
            result.ok = false;
            result.message = L"Could not write the Chrome/Edge host manifest.";
            return result;
        }
        RegWriteString(HKEY_CURRENT_USER, L"Software\\Google\\Chrome\\NativeMessagingHosts\\com.audiodycker.youtube",
                       L"", chromeManifest);
        RegWriteString(HKEY_CURRENT_USER, L"Software\\Microsoft\\Edge\\NativeMessagingHosts\\com.audiodycker.youtube",
                       L"", chromeManifest);
        registrations += 2;
        details += L"Chrome, Edge; ";
    }

    {
        std::string manifest =
            "{\n  \"name\": \"com.audiodycker.youtube\",\n"
            "  \"description\": \"Audio Ducker YouTube playback detector\",\n"
            "  \"path\": \"" + EscapeJsonString(hostPathUtf8) + "\",\n"
            "  \"type\": \"stdio\",\n"
            "  \"allowed_extensions\": [\"audio-ducker@audiodycker.local\"]\n}\n";
        if (!writeFile(firefoxManifest, manifest)) {
            result.ok = false;
            result.message = L"Could not write the Firefox host manifest.";
            return result;
        }
        // Firefox discovers hosts from this well-known directory.
        std::wstring ffDir = AppDataDirW() + L"\\Mozilla\\NativeMessagingHosts";
        EnsureDir(ffDir);
        writeFile(ffDir + L"\\com.audiodycker.youtube.json", manifest);
        // And from the standard registry location.
        RegWriteString(HKEY_CURRENT_USER, L"Software\\Mozilla\\NativeMessagingHosts\\com.audiodycker.youtube",
                       L"", firefoxManifest);
        registrations++;
        details += L"Firefox; ";
    }

    result.ok = true;
    if (haveExtId)
        result.message = L"Host registered for " + details + L"Next: load the extension from the browser/ folder and enter its ID.";
    else
        result.message = L"Registered for Firefox. Add the Chrome/Edge extension ID to also enable Chrome and Edge.";
    return result;
}

} // namespace ducker
