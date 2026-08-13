#include "detection/BrowserActivityDetector.h"

#include "common/Helpers.h"

namespace ducker {

bool BrowserActivityDetector::IsBrowser(const std::string& processName) const {
    return browsers_.count(LowerAscii(processName)) != 0;
}

void BrowserActivityDetector::OnSessionAdded(const std::string& id, const std::string& processName,
                                             bool active, bool muted) {
    if (!IsBrowser(processName)) return;
    proc_[id] = LowerAscii(processName);
    sessions_[id] = Sess{active, muted};
    Recompute();
}

void BrowserActivityDetector::OnSessionState(const std::string& id, bool active) {
    auto it = sessions_.find(id);
    if (it == sessions_.end()) return;
    if (it->second.active == active) return;
    it->second.active = active;
    Recompute();
}

void BrowserActivityDetector::OnSessionMuted(const std::string& id, bool muted) {
    auto it = sessions_.find(id);
    if (it == sessions_.end()) return;
    if (it->second.muted == muted) return;
    it->second.muted = muted;
    Recompute();
}

void BrowserActivityDetector::OnSessionRemoved(const std::string& id) {
    auto it = proc_.find(id);
    if (it != proc_.end()) {
        proc_.erase(it);
        sessions_.erase(id);
        Recompute();
    }
}

void BrowserActivityDetector::Reset() {
    proc_.clear();
    sessions_.clear();
    if (count_ != 0) {
        count_ = 0;
        if (cb_) cb_(0);
    }
}

void BrowserActivityDetector::Recompute() {
    int count = 0;
    for (const auto& [id, s] : sessions_)
        if (s.active && !s.muted) count++;
    if (count != count_) {
        count_ = count;
        if (cb_) cb_(count);
    }
}

} // namespace ducker
