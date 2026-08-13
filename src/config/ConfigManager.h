#pragma once

#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "common/IniFile.h"

namespace ducker {

struct AppSettings {
    float duckVolume = 0.25f;   // 0.05 .. 0.90
    int fadeDownMs = 500;       // duck fade duration
    int fadeUpMs = 700;         // restore fade duration

    bool browserChrome = true;
    bool browserEdge = true;
    bool browserFirefox = true;
    bool useAudioDetection = true; // detect via browser audio sessions

    bool duckAllOthers = false;    // duck every non-browser app with audio
    bool startWithWindows = false;
    bool verboseLogging = false;
    bool showNotifications = true; // balloon when ducking/restoring

    std::set<std::string, std::less<>> enabledApps; // lowercase exe names
    std::string extensionId;                        // Chrome/Edge unpacked extension id (optional)

    std::vector<std::string> GetEnabledBrowserNames() const;
    bool IsBrowserEnabled(const std::string& lowerName) const;
};

class ConfigManager {
public:
    explicit ConfigManager(std::wstring path);

    bool Load();
    bool Save() const;

    AppSettings Get() const;
    void Set(const AppSettings& settings); // stores and saves + applies startup

    const std::wstring& Path() const { return path_; }

private:
    std::wstring path_;
    mutable std::mutex mu_;
    AppSettings settings_;
};

// Writes the native-messaging host manifests and registry entries so browsers
// can launch AudioDuckerHost.exe (requires the extension from browser/ to be
// loaded; Chrome/Edge need the unpacked extension id).
struct HostRegistrationResult {
    bool ok = false;
    std::wstring message;
};
HostRegistrationResult RegisterNativeMessagingHosts(const std::string& extensionId);

} // namespace ducker
