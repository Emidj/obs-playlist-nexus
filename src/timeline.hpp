#pragma once
#include <algorithm>
#include <cstdint>

namespace nexus {
// Integer nanoseconds: preserve the planned boundary rather than resetting to
// the time of the render callback (which would accumulate drift).
struct Timeline {
    int64_t origin = -1;
    int64_t paused_at = -1;
    uint64_t transitions = 0;
    uint64_t continuity_failures = 0;
    bool stalled = false;
    void reset() { *this = Timeline{}; }
    void pause(int64_t now) { if (paused_at < 0) paused_at = now; }
    void resume(int64_t now) {
        if (paused_at >= 0) {
            if (origin >= 0) origin += std::max<int64_t>(0, now - paused_at);
            paused_at = -1;
        }
    }
    int64_t local(int64_t now) const {
        return origin < 0 ? 0 : std::max<int64_t>(0, (paused_at >= 0 ? paused_at : now) - origin);
    }
    // OBS's integer 60 Hz clock can land a few nanoseconds below a rational
    // media boundary. Do not turn that rounding error into a whole held frame.
    int64_t presentation(int64_t now) const { return local(now) + 1000; }
    void start(int64_t now) { if (origin < 0) origin = now; }
    void miss() { if (!stalled) ++continuity_failures; stalled = true; }
    void switch_clip(int64_t now, int64_t duration) {
        origin = stalled ? now : origin + duration;
        ++transitions;
        stalled = false;
    }
};
}
