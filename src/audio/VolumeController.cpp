#include "audio/VolumeController.h"

#include <vector>

#include "audio/AudioSession.h"
#include "common/Helpers.h"

namespace ducker {

void VolumeController::StartFade(AudioSession* session, const std::string& id, float from, float to,
                                 int durationMs) {
    if (!session) return;
    fader_.Start(id, from, to, NowMs(), durationMs);
    entries_[id] = Entry{session, from, to};
    if (durationMs <= 0) Tick(NowMs()); // apply immediately
}

void VolumeController::SetNow(AudioSession* session, const std::string& id, float volume) {
    if (!session) return;
    fader_.Cancel(id);
    entries_.erase(id);
    session->SetVolume(volume);
}

void VolumeController::Cancel(const std::string& id) {
    fader_.Cancel(id);
    entries_.erase(id);
}

void VolumeController::CancelAll() {
    fader_.CancelAll();
    entries_.clear();
}

bool VolumeController::Tick(int64_t nowMs) {
    // Work over a copy: entries are mutated as fades complete.
    std::vector<std::string> ids;
    ids.reserve(entries_.size());
    for (const auto& [id, _] : entries_) ids.push_back(id);

    for (const auto& id : ids) {
        if (!fader_.IsActive(id)) continue;
        auto [value, done] = fader_.Step(id, nowMs);
        auto eit = entries_.find(id);
        if (eit == entries_.end()) continue;
        AudioSession* s = eit->second.session;
        if (s) s->SetVolume(value);
        if (done) {
            entries_.erase(id);
            if (completeCb_) completeCb_(id, value);
        }
    }
    return fader_.ActiveCount() > 0;
}

} // namespace ducker
