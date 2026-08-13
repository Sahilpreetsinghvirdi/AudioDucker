#include "ducking/DuckingManager.h"

#include "common/Helpers.h"

namespace ducker {

namespace {
constexpr float kEps = vol::kEps;

std::string DisplayName(const std::string& name) {
    if (name.empty()) return "unknown application";
    return name;
}
} // namespace

DuckingManager::DuckingManager(Logger& log, VolumeController& controller)
    : log_(log), controller_(controller) {
    machine_.SetListener(this);
    // The detector fires this on the audio thread whenever its playback count
    // changes; push it into the state machine so ducking actually starts and
    // stops in response to browser audio. Without this, the count was only ever
    // replayed at Configure/SetEnabled time.
    browserDetector_.SetCallback([this](int count) {
        if (!enabled_) return;
        log_.Debug("Browser playback count: ", count);
        machine_.SetSourceCount(BrowserActivityDetector::kSourceName, count);
        activeCount_ = machine_.ActiveSourceCount();
    });
}

void DuckingManager::Attach(AudioSessionManager* audio) {
    audio_ = audio;
    controller_.SetCompleteCallback([this](const std::string& id, float finalVolume) {
        auto pr = pendingRestores_.find(id);
        if (pr != pendingRestores_.end()) {
            float restoreTo = pr->second;
            pendingRestores_.erase(pr);
            machine_.OnRestoreFadeComplete(id, restoreTo);
            return;
        }
        auto it = sessions_.find(id);
        if (it != sessions_.end()) it->second.duckFadeDone = true;
        machine_.OnDuckFadeComplete(id, finalVolume);
    });
}

void DuckingManager::RunOnAudio(std::function<void()> fn) {
    if (audio_ && audio_->IsRunning())
        audio_->Post(std::move(fn));
    else
        fn();
}

void DuckingManager::Configure(const AppSettings& settings) {
    RunOnAudio([this, settings] {
        settings_ = settings;
        machine_.SetDuckVolume(settings.duckVolume);
        std::set<std::string> browsers;
        for (const auto& name : settings.GetEnabledBrowserNames()) browsers.insert(name);
        bool detectionChanged = useAudioDetection_ != settings.useAudioDetection;
        useAudioDetection_ = settings.useAudioDetection;
        if (detectionChanged) browserDetector_.Reset(); // emits count 0 if ducking
        browserDetector_.SetEnabledBrowsers(useAudioDetection_ ? browsers
                                                               : std::set<std::string>{});
        log_.SetVerbose(settings.verboseLogging);
        log_.Info("Settings applied: duck=", Percent(settings.duckVolume),
                  " down=", settings.fadeDownMs, "ms up=", settings.fadeUpMs, "ms");
        ReplayDetectionCounts();
    });
}

void DuckingManager::Notify(int eventId) {
    if (notify_) notify_(eventId);
}

bool DuckingManager::IsTargetApp(const std::string& processName) const {
    if (settings_.IsBrowserEnabled(processName)) return false; // never duck the browser itself
    if (settings_.duckAllOthers) return true;
    return settings_.enabledApps.count(LowerAscii(processName)) != 0;
}

// ---------------------------------------------------------------------------
// audio thread callbacks
// ---------------------------------------------------------------------------

void DuckingManager::OnSessionAdded(const SessionInfo& info, AudioSession* session) {
    if (info.system || info.processId == 0 || !session) {
        log_.Debug("Ignore session: proc=", info.processName, " system=", info.system,
                   " pid=", info.processId);
        return;
    }

    if (browserDetector_.IsBrowser(info.processName)) {
        log_.Debug("Browser session: ", info.processName, " active=", info.active,
                   " muted=", info.muted);
        sessions_[info.id] = Record{session, info.processName, true, info.active, false};
        browserDetector_.OnSessionAdded(info.id, info.processName, info.active, info.muted);
        return;
    }

    if (!IsTargetApp(info.processName)) {
        log_.Debug("Not a target: ", info.processName);
        return;
    }

    log_.Debug("Target session: ", info.processName, " active=", info.active);
    sessions_[info.id] = Record{session, info.processName, false, false, false};
    machine_.AddSession(info.id, info.volume, info.muted);
    if (machine_.IsDucking())
        log_.Info("Application started while ducking active: ", DisplayName(info.processName));
}

void DuckingManager::OnSessionRemoved(const std::string& id) {
    auto it = sessions_.find(id);
    if (it == sessions_.end()) return;
    if (it->second.isBrowser) {
        browserDetector_.OnSessionRemoved(id);
    } else {
        machine_.RemoveSession(id);
    }
    sessions_.erase(it);
}

void DuckingManager::OnVolumeChanged(const std::string& id, float volume, bool muted) {
    auto it = sessions_.find(id);
    if (it == sessions_.end()) return;
    Record& rec = it->second;

    if (rec.isBrowser) {
        browserDetector_.OnSessionMuted(id, muted);
        return;
    }

    machine_.UpdateSessionVolume(id, volume);
    machine_.UpdateSessionMuted(id, muted);

    // An application muted when the duck started has just been unmuted and was
    // never faded: fade it now.
    if (machine_.IsDucking() && machine_.HasTarget(id) && !muted && !rec.duckFadeDone &&
        !controller_.IsFading(id)) {
        float target = machine_.TargetVolume(id);
        if (target + kEps < volume) {
            controller_.StartFade(rec.session, id, volume, target, settings_.fadeDownMs);
            log_.Info("Ducking ", DisplayName(rec.processName), ": ", Percent(volume), " -> ",
                      Percent(target));
        } else {
            rec.duckFadeDone = true;
            machine_.OnDuckFadeComplete(id, volume);
        }
    }
}

void DuckingManager::OnStateChanged(const std::string& id, bool active) {
    auto it = sessions_.find(id);
    if (it == sessions_.end()) {
        log_.Debug("State change for untracked session: ", id, " active=", active);
        return;
    }
    if (!it->second.isBrowser) return;
    log_.Debug("Browser session state: ", id, " active=", active);
    it->second.wasActive = active;
    browserDetector_.OnSessionState(id, active);
}

void DuckingManager::OnDeviceChanged() {
    log_.Info("Audio device changed - clearing all snapshots");
    controller_.CancelAll();
    sessions_.clear();
    pendingRestores_.clear();
    machine_.ResetAll();
    browserDetector_.Reset(); // emits count 0
    ReplayDetectionCounts();
}

void DuckingManager::OnTick(int64_t nowMs) {
    if (nowMs - lastPollMs_ < 1000) return;
    lastPollMs_ = nowMs;

    // The browser extension helper host may have died (browser closed) without
    // telling us. If its process no longer exists, drop its playback count.
    for (auto it = extensionCounts_.begin(); it != extensionCounts_.end();) {
        if (!ProcessExists(it->first)) {
            machine_.SetSourceCount("yt:" + std::to_string(it->first), 0);
            it = extensionCounts_.erase(it);
        } else {
            ++it;
        }
    }

    for (auto& [id, rec] : sessions_) {
        AudioSession* s = rec.session;
        if (!s) continue;
        if (rec.isBrowser) {
            bool active = s->IsActive();
            float v = 0.0f;
            bool m = false;
            s->GetVolume(v, m);
            if (!browserDetector_.Has(id)) {
                // Detection was reset (e.g. useAudioDetection toggled): re-register.
                rec.wasActive = active;
                browserDetector_.OnSessionAdded(id, rec.processName, active, m);
            } else {
                if (active != rec.wasActive) {
                    rec.wasActive = active;
                    browserDetector_.OnSessionState(id, active);
                }
                browserDetector_.OnSessionMuted(id, m);
            }
        } else if (machine_.HasTarget(id)) {
            float v = 0.0f;
            bool m = false;
            if (s->GetVolume(v, m)) machine_.UpdateSessionVolume(id, v);
        }
    }
}

// ---------------------------------------------------------------------------
// UI thread commands
// ---------------------------------------------------------------------------

void DuckingManager::SetEnabled(bool enabled) {
    enabled_ = enabled;
    RunOnAudio([this, enabled] {
        if (!enabled) {
            log_.Info("Audio Ducker disabled - restoring if needed");
            for (const auto& [source, _] : machine_.SourceCounts()) machine_.SetSourceCount(source, 0);
        } else {
            log_.Info("Audio Ducker enabled");
            ReplayDetectionCounts();
        }
    });
}

void DuckingManager::ForceDuck() {
    RunOnAudio([this] {
        if (!enabled_) {
            log_.Info("Ignore \"Duck now\": disabled");
            return;
        }
        log_.Info("Manual duck requested");
        machine_.SetForcedDuck(true);
    });
}

void DuckingManager::ForceRestore() {
    RunOnAudio([this] {
        log_.Info("Manual restore requested");
        machine_.SetForcedDuck(false);
    });
}

void DuckingManager::SetExtensionCount(DWORD hostPid, int count) {
    RunOnAudio([this, hostPid, count] {
        std::string source = "yt:" + std::to_string(hostPid);
        if (count <= 0) {
            extensionCounts_.erase(hostPid);
            machine_.SetSourceCount(source, 0);
        } else {
            extensionCounts_[hostPid] = count;
            if (enabled_) machine_.SetSourceCount(source, count);
        }
        activeCount_ = machine_.ActiveSourceCount();
    });
}

void DuckingManager::ReplayDetectionCounts() {
    if (!enabled_) return;
    machine_.SetSourceCount(BrowserActivityDetector::kSourceName, browserDetector_.Count());
    for (const auto& [pid, count] : extensionCounts_)
        machine_.SetSourceCount("yt:" + std::to_string(pid), count);
    activeCount_ = machine_.ActiveSourceCount();
}

// ---------------------------------------------------------------------------
// DuckingStateMachine::Listener
// ---------------------------------------------------------------------------

void DuckingManager::OnDuckStarted(int sourceCount) {
    duckingFlag_ = true;
    activeCount_ = machine_.ActiveSourceCount();
    log_.Info("Playback detected (", sourceCount, " active source(s)) - ducking background audio");
    Notify(1);
}

void DuckingManager::OnDuckStopped() {
    duckingFlag_ = false;
    activeCount_ = machine_.ActiveSourceCount();
    log_.Info("Playback stopped - restoring background audio");
    Notify(2);
}

void DuckingManager::OnTargetAdded(const DuckingStateMachine::TargetInfo& info) {
    auto it = sessions_.find(info.id);
    if (it == sessions_.end()) return;
    Record& rec = it->second;
    AudioSession* s = rec.session;
    if (!s) return;

    rec.duckFadeDone = false;
    pendingRestores_.erase(info.id);

    if (info.muted) {
        log_.Debug("Skipping muted session: ", DisplayName(rec.processName));
        return;
    }
    if (info.target + kEps >= info.current) {
        rec.duckFadeDone = true;
        machine_.OnDuckFadeComplete(info.id, info.current);
        log_.Info(DisplayName(rec.processName), " already at or below duck level (",
                  Percent(info.current), ") - no change");
        return;
    }
    controller_.StartFade(s, info.id, info.current, info.target, settings_.fadeDownMs);
    log_.Info("Ducking ", DisplayName(rec.processName), ": ", Percent(info.current), " -> ",
              Percent(info.target));
}

void DuckingManager::OnTargetUpdated(const DuckingStateMachine::TargetInfo& info) {
    auto it = sessions_.find(info.id);
    std::string name = (it != sessions_.end()) ? DisplayName(it->second.processName) : info.id;
    log_.Info("Manual volume change on ", name, " while ducking (now ", Percent(info.original),
              ") - restoring to that value later");
}

void DuckingManager::OnTargetRestore(const DuckingStateMachine::TargetInfo& info) {
    auto it = sessions_.find(info.id);
    if (it == sessions_.end()) return;
    Record& rec = it->second;
    AudioSession* s = rec.session;
    if (!s) return;

    pendingRestores_[info.id] = info.original;
    if (info.muted) {
        machine_.OnRestoreFadeComplete(info.id, info.original);
        pendingRestores_.erase(info.id);
        return;
    }
    float from = info.current;
    if (std::fabs(from - info.original) < kEps) {
        machine_.OnRestoreFadeComplete(info.id, info.original);
        pendingRestores_.erase(info.id);
        log_.Info("Restoring ", DisplayName(rec.processName), ": already at ", Percent(info.original));
        return;
    }
    controller_.StartFade(s, info.id, from, info.original, settings_.fadeUpMs);
    log_.Info("Restoring ", DisplayName(rec.processName), ": ", Percent(from), " -> ",
              Percent(info.original));
}

void DuckingManager::OnTargetRemoved(const std::string& id) {
    controller_.Cancel(id);
    pendingRestores_.erase(id);
    auto it = sessions_.find(id);
    if (it != sessions_.end())
        log_.Info(DisplayName(it->second.processName), " closed while ducked - snapshot discarded");
}

} // namespace ducker
