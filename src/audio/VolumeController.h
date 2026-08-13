#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>

#include "core/VolumeFader.h"

namespace ducker {

class AudioSession;

// Applies smooth volume fades to audio sessions. Must be driven from the audio
// worker thread only (all SetVolume calls happen on the same STA that owns the
// session objects).
class VolumeController {
public:
    using CompleteCallback = std::function<void(const std::string& id, float finalVolume)>;

    void SetCompleteCallback(CompleteCallback cb) { completeCb_ = std::move(cb); }

    void StartFade(AudioSession* session, const std::string& id, float from, float to, int durationMs);
    void SetNow(AudioSession* session, const std::string& id, float volume);
    void Cancel(const std::string& id);
    void CancelAll();

    bool IsFading(const std::string& id) const { return fader_.IsActive(id); }
    int ActiveCount() const { return fader_.ActiveCount(); }

    // Advances all fades; call periodically. Returns true while fades remain.
    bool Tick(int64_t nowMs);

private:
    struct Entry {
        AudioSession* session = nullptr;
        float from = 0.0f;
        float to = 0.0f;
    };

    VolumeFader fader_;
    std::map<std::string, Entry> entries_;
    CompleteCallback completeCb_;
};

} // namespace ducker
