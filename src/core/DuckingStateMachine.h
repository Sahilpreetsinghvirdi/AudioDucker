#pragma once

#include <map>
#include <string>

#include "core/PlaybackDetector.h"
#include "core/VolumeSnapshot.h"

namespace ducker {

// The duck/restore state machine. Pure logic (no COM, no UI); every decision
// about when to duck, what to snapshot and what to restore lives here and is
// unit-tested. The audio layer (DuckingManager) reacts to the events it emits
// and reports fade completion back into it.
class DuckingStateMachine {
public:
    struct TargetInfo {
        std::string id;
        float original = 0.0f; // volume captured before ducking (or user's new baseline)
        float target = 0.0f;   // volume to duck to
        float current = 0.0f;  // last known volume
        bool muted = false;
    };

    class Listener {
    public:
        virtual ~Listener() = default;
        virtual void OnDuckStarted(int sourceCount) = 0;
        virtual void OnDuckStopped() = 0;
        virtual void OnTargetAdded(const TargetInfo& info) = 0;
        virtual void OnTargetUpdated(const TargetInfo& info) = 0;   // original volume changed mid-duck
        virtual void OnTargetRestore(const TargetInfo& info) = 0;   // restore to info.original
        virtual void OnTargetRemoved(const std::string& id) = 0;    // session vanished; nothing to restore
    };

    void SetListener(Listener* l) { listener_ = l; }
    void SetDuckVolume(float v) { duckVolume_ = vol::Clamp01(v); }

    // --- detection -------------------------------------------------------
    void SetSourceCount(const std::string& source, int count);
    void RemoveSource(const std::string& source);
    bool IsDucking() const { return ducking_; }
    int ActiveSourceCount() const { return detector_.TotalCount(); }
    std::map<std::string, int> SourceCounts() const { return detector_.Snapshot(); }

    // Tray "Duck now" / "Restore now".
    void SetForcedDuck(bool force);

    // --- session tracking ------------------------------------------------
    void AddSession(const std::string& id, float volume, bool muted);
    void UpdateSessionVolume(const std::string& id, float volume);
    void UpdateSessionMuted(const std::string& id, bool muted);
    void RemoveSession(const std::string& id);

    void OnDuckFadeComplete(const std::string& id, float finalVolume);
    void OnRestoreFadeComplete(const std::string& id, float restoredVolume);

    // --- introspection ----------------------------------------------------
    bool IsSessionTracked(const std::string& id) const { return sessions_.count(id) != 0; }
    bool HasTarget(const std::string& id) const { return targets_.count(id) != 0; }
    bool TargetFaded(const std::string& id) const {
        auto it = targets_.find(id);
        return it != targets_.end() && it->second.faded;
    }
    float TargetVolume(const std::string& id) const {
        auto it = targets_.find(id);
        return it != targets_.end() ? it->second.duckTarget : 0.0f;
    }
    size_t TargetCount() const { return targets_.size(); }

    // Device change: forget every snapshot and baseline without restoring.
    void ResetAll();

private:
    void RecomputeDucking();
    void BeginDuck(int sourceCount);
    void BeginRestore();
    void TryAddTarget(const std::string& id);

    struct Session {
        float baseVolume = 0.0f; // the volume this app "wants" right now
        bool muted = false;
    };
    struct Target {
        float original = 0.0f;
        float duckTarget = 0.0f;
        float current = 0.0f;
        bool faded = false;      // duck fade finished
        bool restoring = false;
        float restoreTo = 0.0f;
    };

    PlaybackDetector detector_;
    std::map<std::string, Session> sessions_;
    std::map<std::string, Target> targets_;
    float duckVolume_ = 0.25f;
    bool ducking_ = false;
    bool forcedDuck_ = false;
    Listener* listener_ = nullptr;
};

} // namespace ducker
