#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <utility>

namespace ducker {

// Pure linear volume interpolator. Given a start time, start/end volume and a
// duration it computes the current volume for any time. Starting a new fade for
// an id replaces the previous one. This class contains no audio code and is
// fully unit-testable.
class VolumeFader {
public:
    struct Fade {
        float from = 0.0f;
        float to = 0.0f;
        int64_t startMs = 0;
        int durationMs = 0;
    };

    void Start(const std::string& id, float from, float to, int64_t nowMs, int durationMs);
    void Cancel(const std::string& id);
    void CancelAll();

    // Returns the interpolated volume at nowMs and whether the fade finished.
    std::pair<float, bool> Step(const std::string& id, int64_t nowMs);

    bool IsActive(const std::string& id) const { return fades_.count(id) != 0; }
    int ActiveCount() const { return static_cast<int>(fades_.size()); }

private:
    std::map<std::string, Fade> fades_;
};

} // namespace ducker
