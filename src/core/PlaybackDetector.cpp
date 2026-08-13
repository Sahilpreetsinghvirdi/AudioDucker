#include "core/PlaybackDetector.h"

#include <algorithm>

namespace ducker {

void PlaybackDetector::SetSourceCount(const std::string& source, int count) {
    if (count <= 0)
        counts_.erase(source);
    else
        counts_[source] = count;
    Recompute();
    Notify();
}

void PlaybackDetector::RemoveSource(const std::string& source) {
    counts_.erase(source);
    Recompute();
    Notify();
}

void PlaybackDetector::Clear() {
    counts_.clear();
    Recompute();
    Notify();
}

void PlaybackDetector::Recompute() {
    int total = 0;
    for (const auto& [_, c] : counts_) total += c;
    total_ = total;
    bool active = total_ > 0;
    active_ = active;
}

void PlaybackDetector::Notify() {
    if (cb_) cb_(active_, total_);
}

} // namespace ducker
