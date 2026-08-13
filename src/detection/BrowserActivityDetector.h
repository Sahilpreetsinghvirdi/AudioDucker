#pragma once

#include <functional>
#include <map>
#include <set>
#include <string>

namespace ducker {

// Counts "playing" browser sessions. A browser session counts as playing when
// its audio session is Active and not muted. This is what lets us distinguish
// "Chrome open but silent" (do nothing) from "Chrome is producing audio"
// (duck). It cannot distinguish YouTube from any other browser media - the
// browser extension helper adds that precision on top.
class BrowserActivityDetector {
public:
    using Callback = std::function<void(int count)>;
    static constexpr const char* kSourceName = "browser-audio";

    void SetCallback(Callback cb) { cb_ = std::move(cb); }
    void SetEnabledBrowsers(std::set<std::string> browsers) { browsers_ = std::move(browsers); }
    bool IsBrowser(const std::string& processName) const;

    void OnSessionAdded(const std::string& id, const std::string& processName, bool active, bool muted);
    void OnSessionState(const std::string& id, bool active);
    void OnSessionMuted(const std::string& id, bool muted);
    void OnSessionRemoved(const std::string& id);
    void Reset();

    int Count() const { return count_; }

private:
    struct Sess {
        bool active = false;
        bool muted = false;
    };
    void Recompute();

    std::map<std::string, std::string> proc_; // id -> process name (browser sessions only)
    std::map<std::string, Sess> sessions_;
    std::set<std::string> browsers_;
    int count_ = 0;
    Callback cb_;
};

} // namespace ducker
