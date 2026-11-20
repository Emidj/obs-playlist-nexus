#include "decoder.hpp"
#include "timeline.hpp"
#include <chrono>
#include <iostream>
#include <random>
#include <thread>
#include <stdexcept>
using namespace std::chrono;
static int64_t now() { return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count(); }
static void wait_ready(nexus::Decoder &d) {
    const auto deadline = steady_clock::now() + seconds(10);
    while (steady_clock::now() < deadline) {
        auto s = d.sample(0);
        if (s.ready) return;
        if (s.failed) throw std::runtime_error("decoder rejected fixture");
        std::this_thread::sleep_for(milliseconds(1));
    }
    throw std::runtime_error("decoder readiness timeout");
}
int main(int argc, char **argv) {
    try {
        if (argc < 3) throw std::runtime_error("usage: decoder_tests clip1 clip2 [...]");
        std::vector<std::string> files(argv+1, argv+argc);
        for (bool random : {false, true}) {
            nexus::Decoder a, b;
            nexus::Decoder *active = &a, *standby = &b;
            size_t index = 0, next = 1;
            std::mt19937 rng(42);
            active->request(files[index]); standby->request(files[next]);
            wait_ready(*active); wait_ready(*standby);
            nexus::Timeline timeline; timeline.start(now());
            uint64_t frames = 0, empty = 0;
            auto wake = steady_clock::now();
            const auto deadline = wake + seconds(90);
            while (timeline.transitions < 100 && steady_clock::now() < deadline) {
                const auto time = now();
                auto sample = active->sample(timeline.local(time));
                if (sample.failed) throw std::runtime_error("unexpected fixture decode failure");
                if (sample.starved) timeline.miss();
                if (sample.ended) {
                    auto prepared = standby->sample(timeline.stalled ? 0 : std::max<int64_t>(0, timeline.local(time)-sample.end));
                    if (prepared.ready) {
                        timeline.switch_clip(time, sample.end);
                        std::swap(active, standby); index = next;
                        next = random ? (index+1+rng()%(files.size()-1))%files.size() : (index+1)%files.size();
                        standby->request(files[next]);
                        sample = prepared;
                    } else timeline.miss();
                }
                if (!sample.frame || sample.frame->rgba.empty()) ++empty;
                else {
                    // Test fixtures must contain a colored pixel, not black.
                    const auto &p = sample.frame->rgba;
                    if (int(p[0])+int(p[1])+int(p[2]) < 50) throw std::runtime_error("unexpected black test pixel");
                    if (sample.frame->width != 1920 || sample.frame->height != 1080) throw std::runtime_error("expected 1080p fixtures");
                }
                ++frames; wake += nanoseconds(16666667); std::this_thread::sleep_until(wake);
            }
            std::cout << (random ? "random" : "sequential") << ": transitions=" << timeline.transitions
                << " samples=" << frames << " empty=" << empty << " continuity_failures=" << timeline.continuity_failures << std::endl;
            if (timeline.transitions != 100 || empty || timeline.continuity_failures)
                throw std::runtime_error("headless continuity test failed");
        }
        nexus::Decoder invalid;
        invalid.request("C:/NEXUS_TEST_MISSING_FILE.mp4");
        const auto deadline = steady_clock::now() + seconds(5);
        bool failed = false;
        while (steady_clock::now() < deadline && !(failed = invalid.sample(0).failed))
            std::this_thread::sleep_for(milliseconds(1));
        if (!failed) throw std::runtime_error("missing input not reported");
        invalid.request(files[0]); wait_ready(invalid);
        for (int i = 0; i < 30; ++i) invalid.request(files[size_t(i)%files.size()]);
        wait_ready(invalid);
        std::cout << "PASS: missing input, recovery, rapid cancellation and shutdown\n";
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
