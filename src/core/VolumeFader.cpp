#include "core/VolumeFader.h"

#include <algorithm>

namespace ducker {

namespace {
// Smooth-step easing keeps the movement gentle at both ends of the fade.
inline float Ease(float t) {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    return t * t * (3.0f - 2.0f * t);
}
} // namespace

void VolumeFader::Start(const std::string& id, float from, float to, int64_t nowMs, int durationMs) {
    Fade f;
    f.from = from;
    f.to = to;
    f.startMs = nowMs;
    f.durationMs = durationMs > 0 ? durationMs : 0;
    fades_[id] = f;
}

void VolumeFader::Cancel(const std::string& id) { fades_.erase(id); }

void VolumeFader::CancelAll() { fades_.clear(); }

std::pair<float, bool> VolumeFader::Step(const std::string& id, int64_t nowMs) {
    auto it = fades_.find(id);
    if (it == fades_.end()) return {0.0f, true};
    const Fade& f = it->second;

    if (f.durationMs <= 0) {
        float to = f.to;
        fades_.erase(it);
        return {to, true};
    }

    float t = static_cast<float>(nowMs - f.startMs) / static_cast<float>(f.durationMs);
    if (t >= 1.0f) {
        float to = f.to;
        fades_.erase(it);
        return {to, true};
    }
    if (t < 0.0f) t = 0.0f;

    float value = f.from + (f.to - f.from) * Ease(t);
    return {value, false};
}

} // namespace ducker
