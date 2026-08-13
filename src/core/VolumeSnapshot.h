#pragma once

#include <algorithm>
#include <cmath>

namespace ducker::vol {

// Volume policy helpers. All volumes are linear fractions in [0, 1].
// These functions are pure and unit-tested; they contain no COM / audio code.

constexpr float kEps = 0.02f; // 2% tolerance for "user touched the slider"

inline float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// If an application is already quieter than the configured duck level we must
// not raise it ("If Spotify was already at 15%, do not increase it to 25%").
inline float ComputeDuckTarget(float current, float duckVolume) {
    return Clamp01(std::min(current, duckVolume));
}

// True if the current volume differs from the level we ducked to, meaning the
// user (or the application) moved it during the duck.
inline bool UserIntervened(float current, float duckTarget, float eps = kEps) {
    return std::fabs(current - duckTarget) > eps;
}

// Decide what volume to restore to.
//  - If the user changed the volume during the duck, their manual value is the
//    new baseline and must not be overwritten.
//  - Otherwise restore the exact volume the app had before the duck.
inline float ComputeRestoreVolume(float original, float duckTarget, float current, float eps = kEps) {
    if (UserIntervened(current, duckTarget, eps)) return current;
    return original;
}

} // namespace ducker::vol
