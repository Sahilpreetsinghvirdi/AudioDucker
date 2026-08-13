#pragma once

#include <functional>
#include <map>
#include <string>

namespace ducker {

// Aggregates "playback is active" signals coming from several independent
// sources (browser audio sessions, the YouTube extension helper, ...).
// Rule: duck while the total active count across all sources is > 0.
class PlaybackDetector {
public:
    using Callback = std::function<void(bool active, int totalCount)>;

    void SetCallback(Callback cb) { cb_ = std::move(cb); }

    void SetSourceCount(const std::string& source, int count);
    void RemoveSource(const std::string& source);
    void Clear();

    bool IsActive() const { return active_; }
    int TotalCount() const { return total_; }
    std::map<std::string, int> Snapshot() const { return counts_; }

private:
    void Recompute();
    void Notify();

    std::map<std::string, int> counts_;
    bool active_ = false;
    int total_ = 0;
    Callback cb_;
};

} // namespace ducker
