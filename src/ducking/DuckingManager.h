#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <string>

#include "audio/AudioSession.h"
#include "audio/AudioSessionManager.h"
#include "audio/VolumeController.h"
#include "config/ConfigManager.h"
#include "core/DuckingStateMachine.h"
#include "detection/BrowserActivityDetector.h"
#include "utils/Logger.h"

namespace ducker {

// Orchestrates detection -> snapshot -> fade. All state lives on the audio
// worker thread; commands arriving from the UI thread are marshalled onto it
// through the task queue. A small set of atomics mirror the current state for
// the tray tooltip.
class DuckingManager : public DuckingStateMachine::Listener {
public:
    explicit DuckingManager(Logger& log, VolumeController& controller);

    void Attach(AudioSessionManager* audio);
    void Configure(const AppSettings& settings);
    void SetNotifyCallback(std::function<void(int eventId)> cb) { notify_ = std::move(cb); }

    // ---- audio thread callbacks (from AudioSessionManager) ----------------
    void OnSessionAdded(const SessionInfo& info, AudioSession* session);
    void OnSessionRemoved(const std::string& id);
    void OnVolumeChanged(const std::string& id, float volume, bool muted);
    void OnStateChanged(const std::string& id, bool active);
    void OnDeviceChanged();
    void OnTick(int64_t nowMs);

    // ---- commands from the UI thread -------------------------------------
    void SetEnabled(bool enabled);
    void ForceDuck();
    void ForceRestore();
    void SetExtensionCount(DWORD hostPid, int count);

    // ---- state mirrors (any thread) ---------------------------------------
    bool IsDucking() const { return duckingFlag_.load(); }
    bool IsEnabled() const { return enabled_.load(); }
    int ActiveSourceCount() const { return activeCount_.load(); }

    // ---- DuckingStateMachine::Listener ------------------------------------
    void OnDuckStarted(int sourceCount) override;
    void OnDuckStopped() override;
    void OnTargetAdded(const DuckingStateMachine::TargetInfo& info) override;
    void OnTargetUpdated(const DuckingStateMachine::TargetInfo& info) override;
    void OnTargetRestore(const DuckingStateMachine::TargetInfo& info) override;
    void OnTargetRemoved(const std::string& id) override;

private:
    struct Record {
        AudioSession* session = nullptr;
        std::string processName;
        bool isBrowser = false;
        bool wasActive = false;
        bool duckFadeDone = false;
    };

    bool IsTargetApp(const std::string& processName) const;
    void RunOnAudio(std::function<void()> fn);
    void ReplayDetectionCounts();
    void Notify(int eventId);

    Logger& log_;
    VolumeController& controller_;
    AudioSessionManager* audio_ = nullptr;
    DuckingStateMachine machine_;
    BrowserActivityDetector browserDetector_;
    AppSettings settings_;

    std::map<std::string, Record> sessions_;
    std::map<std::string, float> pendingRestores_;
    std::map<DWORD, int> extensionCounts_; // native messaging host pid -> active count

    std::atomic<bool> enabled_{true};
    std::atomic<bool> duckingFlag_{false};
    std::atomic<int> activeCount_{0};
    int64_t lastPollMs_ = 0;
    std::function<void(int)> notify_;
};

} // namespace ducker
