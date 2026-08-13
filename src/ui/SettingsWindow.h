#pragma once

#include <windows.h>

#include <functional>
#include <string>
#include <vector>

#include "config/ConfigManager.h"

namespace ducker {

struct DiscoveredApp {
    std::string processName; // lowercase exe
    std::wstring displayName;
};

struct SettingsCallbacks {
    std::function<void(const AppSettings&)> onApply;
    std::function<std::vector<DiscoveredApp>()> discoverApps; // blocking (UI thread)
};

// Opens the modal settings window owned by `owner`.
void ShowSettingsWindow(HWND owner, const AppSettings& initial, SettingsCallbacks cb);

} // namespace ducker
