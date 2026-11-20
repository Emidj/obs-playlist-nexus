#pragma once
#include <atomic>
#include <array>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace nexus {
struct Frame {
    int width = 0, height = 0;
    int64_t pts = 0, duration = 0;
    std::vector<uint8_t> rgba;
};
struct Sample {
    std::shared_ptr<Frame> frame;
    bool ready = false, ended = false, failed = false, starved = false;
    int64_t end = 0;
};
class Decoder {
public:
    Decoder();
    ~Decoder();
    void request(std::string path);
    Sample sample(int64_t time);
    bool prepared() const;
    bool failed() const;
    int64_t duration_hint() const;
    void set_changed(std::function<void()> callback) { changed_=std::move(callback); }
    void stop();
private:
    struct Job {
        uint64_t generation = 0;
        std::string path;
        std::array<std::shared_ptr<Frame>, 8> frames;
        std::atomic<uint64_t> read{0}, write{0}, wake{0};
        std::atomic<bool> eof{false}, failed{false};
        std::atomic<int64_t> end{0};
        std::atomic<int64_t> duration{0};
        std::shared_ptr<Frame> current; // Consumer only.
    };
    void run();
    void decode(const std::shared_ptr<Job> &job, uint64_t generation);
    std::atomic<std::shared_ptr<Job>> job_;
    std::atomic<uint64_t> generation_{0};
    std::atomic<bool> stop_{false};
    std::function<void()> changed_; // Set before any job; joined before teardown.
    std::thread thread_;
};
}
