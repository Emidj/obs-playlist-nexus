#include "timeline.hpp"
#include <cstdlib>
#include <iostream>
static void require(bool value, const char *message) {
    if (!value) { std::cerr << message << '\n'; std::exit(1); }
}
int main() {
    nexus::Timeline rounded;
    rounded.start(0);
    require(rounded.local(16666666) < 16666667, "fixture must straddle integer rounding");
    require(rounded.presentation(16666666) >= 16666667, "integer clock rounding must not hold a full frame");
    require(rounded.presentation(16000000) < 16666667, "rounding allowance must not advance a normal frame early");
    nexus::Timeline t;
    t.start(100);
    for (int i = 0; i < 1000; ++i) {
        const auto boundary = t.origin + 1'000'000'000;
        t.switch_clip(boundary + 5'000'000, 1'000'000'000);
        require(t.local(boundary + 5'000'000) == 5'000'000, "boundary accumulated callback drift");
    }
    require(t.transitions == 1000 && t.continuity_failures == 0, "transition counters");
    const auto now = t.origin + 123;
    t.pause(now); t.pause(now + 50);
    require(t.local(now + 1000) == 123, "pause must freeze clock");
    t.resume(now + 1000);
    require(t.local(now + 1000) == 123, "resume must preserve position");
    t.miss(); t.miss();
    require(t.continuity_failures == 1, "one incident per continuous stall");
    t.switch_clip(now + 2000, 100);
    require(t.local(now + 2000) == 0, "recovery must rebase instead of skipping new clip");
    t.reset(); require(t.origin == -1 && t.transitions == 0, "reset");
    std::cout << "PASS: 1000 boundaries, pause/resume, stall accounting and recovery\n";
}
