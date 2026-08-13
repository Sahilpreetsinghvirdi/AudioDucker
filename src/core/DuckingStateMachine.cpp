#include "core/DuckingStateMachine.h"

#include <vector>

namespace ducker {

void DuckingStateMachine::SetSourceCount(const std::string& source, int count) {
    detector_.SetSourceCount(source, count);
    RecomputeDucking();
}

void DuckingStateMachine::RemoveSource(const std::string& source) {
    detector_.RemoveSource(source);
    RecomputeDucking();
}

void DuckingStateMachine::SetForcedDuck(bool force) {
    forcedDuck_ = force;
    // A manual "Restore now" must win over any still-active source, otherwise
    // the tray button silently does nothing while the browser keeps playing.
    restoreOverride_ = !force;
    RecomputeDucking();
}

void DuckingStateMachine::RecomputeDucking() {
    bool want = forcedDuck_ || detector_.IsActive();
    if (want && !ducking_) {
        restoreOverride_ = false;
        ducking_ = true;
        BeginDuck(detector_.TotalCount());
    } else if ((!want || restoreOverride_) && ducking_) {
        restoreOverride_ = false;
        ducking_ = false;
        BeginRestore();
    }
}

void DuckingStateMachine::BeginDuck(int sourceCount) {
    if (listener_) listener_->OnDuckStarted(sourceCount);
    for (const auto& [id, _] : sessions_) TryAddTarget(id);
}

void DuckingStateMachine::BeginRestore() {
    // Iterate over a stable copy; restoring does not modify targets_ yet.
    std::vector<std::string> ids;
    ids.reserve(targets_.size());
    for (const auto& [id, _] : targets_) ids.push_back(id);

    for (const auto& id : ids) {
        auto it = targets_.find(id);
        if (it == targets_.end()) continue;
        Target& t = it->second;
        t.restoreTo = vol::ComputeRestoreVolume(t.original, t.duckTarget, t.current);
        t.restoring = true;
        auto sIt = sessions_.find(id);
        bool muted = sIt != sessions_.end() && sIt->second.muted;
        if (listener_)
            listener_->OnTargetRestore(TargetInfo{id, t.restoreTo, t.duckTarget, t.current, muted});
    }
    if (listener_) listener_->OnDuckStopped();
}

void DuckingStateMachine::TryAddTarget(const std::string& id) {
    if (targets_.count(id)) return;
    auto sIt = sessions_.find(id);
    if (sIt == sessions_.end()) return;

    Session& s = sIt->second;
    float duckTarget = vol::ComputeDuckTarget(s.baseVolume, duckVolume_);
    Target t;
    t.original = s.baseVolume;
    t.duckTarget = duckTarget;
    t.current = s.baseVolume;
    t.faded = false;
    t.restoring = false;
    targets_[id] = t;

    if (listener_) listener_->OnTargetAdded(TargetInfo{id, t.original, t.duckTarget, t.current, s.muted});
}

void DuckingStateMachine::AddSession(const std::string& id, float volume, bool muted) {
    Session s;
    s.baseVolume = vol::Clamp01(volume);
    s.muted = muted;
    sessions_[id] = s;

    if (ducking_) TryAddTarget(id);
}

void DuckingStateMachine::UpdateSessionVolume(const std::string& id, float volume) {
    volume = vol::Clamp01(volume);

    auto sIt = sessions_.find(id);
    if (sIt != sessions_.end()) sIt->second.baseVolume = volume;

    auto tIt = targets_.find(id);
    if (tIt == targets_.end()) return;
    Target& t = tIt->second;
    if (t.restoring) return;

    t.current = volume;
    if (!t.faded) return; // volume is still moving because of our own fade

    if (vol::UserIntervened(volume, t.duckTarget)) {
        // The user (or the app) moved the volume during the duck. Their new
        // value becomes the baseline we must restore to.
        bool changed = t.original != volume;
        t.original = volume;
        if (changed && listener_) {
            bool muted = sIt != sessions_.end() && sIt->second.muted;
            listener_->OnTargetUpdated(TargetInfo{id, t.original, t.duckTarget, volume, muted});
        }
    }
}

void DuckingStateMachine::UpdateSessionMuted(const std::string& id, bool muted) {
    auto sIt = sessions_.find(id);
    if (sIt != sessions_.end()) sIt->second.muted = muted;

    if (ducking_ && !muted && targets_.count(id) == 0) {
        // An application that was muted when the duck started has now been
        // unmuted: duck it.
        TryAddTarget(id);
    }
}

void DuckingStateMachine::RemoveSession(const std::string& id) {
    sessions_.erase(id);
    auto it = targets_.find(id);
    if (it != targets_.end()) {
        targets_.erase(it);
        if (listener_) listener_->OnTargetRemoved(id);
    }
}

void DuckingStateMachine::OnDuckFadeComplete(const std::string& id, float finalVolume) {
    auto it = targets_.find(id);
    if (it == targets_.end()) return;
    it->second.faded = true;
    it->second.current = vol::Clamp01(finalVolume);
}

void DuckingStateMachine::OnRestoreFadeComplete(const std::string& id, float restoredVolume) {
    auto it = targets_.find(id);
    if (it != targets_.end()) {
        sessions_[id].baseVolume = vol::Clamp01(restoredVolume);
        targets_.erase(it);
    }
}

void DuckingStateMachine::ResetAll() {
    sessions_.clear();
    targets_.clear();
    ducking_ = false;
    forcedDuck_ = false;
    restoreOverride_ = false;
    detector_.Clear();
}

} // namespace ducker
