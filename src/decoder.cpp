#include "decoder.hpp"
#include <algorithm>
#include <stdexcept>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

namespace nexus {
Decoder::Decoder() : thread_([this] { run(); }) {}
Decoder::~Decoder() { stop(); }
void Decoder::stop() {
    stop_ = true; ++generation_; generation_.notify_all();
    if (auto job = job_.load()) { ++job->wake; job->wake.notify_all(); }
    if(thread_.joinable()) thread_.join();
}
bool Decoder::prepared() const { auto job=job_.load(); return job && job->write.load(std::memory_order_acquire)>0; }
bool Decoder::failed() const { auto job=job_.load(); return job && job->failed.load(); }
int64_t Decoder::duration_hint() const { auto job=job_.load(); return job ? job->duration.load() : 0; }
void Decoder::request(std::string path) {
    auto job = std::make_shared<Job>(); job->path = std::move(path);
    job->generation = generation_.load() + 1;
    const auto generation = job->generation;
    auto old = job_.exchange(std::move(job));
    generation_.store(generation); generation_.notify_one();
    if (old) { ++old->wake; old->wake.notify_one(); }
}
Sample Decoder::sample(int64_t time) {
    Sample s;
    auto job = job_.load();
    if (!job) return s;
    auto read = job->read.load(std::memory_order_relaxed);
    const auto initial_read = read;
    auto write = job->write.load(std::memory_order_acquire);
    s.ready = job->current || read != write;
    while (read != write && job->frames[read % 8]->pts <= time) {
        job->current = std::move(job->frames[read % 8]);
        ++read; job->read.store(read, std::memory_order_release);
        write = job->write.load(std::memory_order_acquire);
    }
    s.frame = job->current;
    const bool eof = job->eof.load(std::memory_order_acquire);
    s.end = job->end; s.failed = job->failed;
    s.ended = eof && read == job->write.load(std::memory_order_acquire) && time >= s.end;
    s.starved = !eof && job->current && read == write && time >= job->current->pts + job->current->duration;
    if (read != initial_read) { ++job->wake; job->wake.notify_one(); }
    return s;
}
void Decoder::run() {
    uint64_t seen = 0;
    while (!stop_) {
        generation_.wait(seen);
        if (stop_) break;
        seen = generation_; auto job = job_.load();
        if (job && job->generation == seen && !job->path.empty()) decode(job, seen);
    }
}
void Decoder::decode(const std::shared_ptr<Job> &job, uint64_t generation) {
    const auto &path = job->path;
    AVFormatContext *format = avformat_alloc_context();
    AVCodecContext *codec = nullptr;
    AVPacket *packet = av_packet_alloc();
    AVFrame *raw = av_frame_alloc();
    SwsContext *scale = nullptr;
    struct Interrupt { Decoder *self; uint64_t generation; } interrupt{this, generation};
    if(format) format->interrupt_callback = {[](void *p) -> int {
        auto *i = static_cast<Interrupt *>(p);
        return i->self->stop_ || i->self->generation_ != i->generation;
    }, &interrupt};
    bool bad = false;
    std::shared_ptr<Frame> pending_frame;
    auto publish = [&](std::shared_ptr<Frame> frame) {
        const size_t capacity = std::max<size_t>(1, std::min<size_t>(8, (64 * 1024 * 1024) / frame->rgba.size()));
        while (!stop_ && generation_ == generation) {
            const auto wake = job->wake.load();
            if (job->write.load(std::memory_order_relaxed) - job->read.load(std::memory_order_acquire) < capacity) break;
            job->wake.wait(wake);
        }
        if (stop_ || generation_ != generation) return;
        const auto write = job->write.load(std::memory_order_relaxed);
        job->end = frame->pts + frame->duration;
        job->frames[write % 8] = std::move(frame);
        job->write.store(write+1, std::memory_order_release);
        if(write==0 && changed_) changed_();
    };
    try {
        if (!format || !packet || !raw) throw std::runtime_error("FFmpeg allocation failed");
        // Local paths only: avoid network protocols and unbounded remote input.
        if (path.find("://") != std::string::npos) throw std::runtime_error("local files only");
        if (avformat_open_input(&format, path.c_str(), nullptr, nullptr) < 0 ||
            avformat_find_stream_info(format, nullptr) < 0) throw std::runtime_error("open");
        const int stream = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (stream < 0) throw std::runtime_error("no video");
        auto *track = format->streams[stream];
        if(track->duration>0 && track->duration!=AV_NOPTS_VALUE)
            job->duration=av_rescale_q(track->duration,track->time_base,AVRational{1,1000000000});
        else if(format->duration>0 && format->duration!=AV_NOPTS_VALUE)
            job->duration=av_rescale_q(format->duration,AVRational{1,AV_TIME_BASE},AVRational{1,1000000000});
        const auto *implementation = avcodec_find_decoder(track->codecpar->codec_id);
        codec = avcodec_alloc_context3(implementation);
        if (!codec || avcodec_parameters_to_context(codec, track->codecpar) < 0) throw std::runtime_error("codec");
        codec->thread_count = 2;
        if (avcodec_open2(codec, implementation, nullptr) < 0) throw std::runtime_error("codec open");
        auto rate = av_guess_frame_rate(format, track, nullptr);
        int64_t fallback = rate.num > 0 && rate.den > 0 ? av_rescale_q(1, av_inv_q(rate), AVRational{1,1000000000}) : 33333333;
        int64_t first = AV_NOPTS_VALUE, previous = -1, last_end = 0;
        bool draining = false, pending = false;
        while (!stop_ && generation_ == generation) {
            const int received = avcodec_receive_frame(codec, raw);
            if (received == AVERROR_EOF) break;
            if (received == AVERROR(EAGAIN)) {
                if (!pending && !draining) {
                    int read = 0;
                    do { av_packet_unref(packet); read = av_read_frame(format, packet); }
                    while (read >= 0 && packet->stream_index != stream && !stop_ && generation_ == generation);
                    if (read < 0) {
                        if (read != AVERROR_EOF) throw std::runtime_error("read");
                        draining = true;
                    } else pending = true;
                }
                const int sent = avcodec_send_packet(codec, draining ? nullptr : packet);
                if (sent == AVERROR(EAGAIN)) continue;
                if (sent < 0 && sent != AVERROR_EOF) throw std::runtime_error("send");
                pending = false; av_packet_unref(packet);
                continue;
            }
            if (received < 0) throw std::runtime_error("receive");
            // Explicit memory ceiling: reject very large inputs in this prototype.
            if (raw->width <= 0 || raw->height <= 0 || raw->width > 3840 || raw->height > 2160)
                throw std::runtime_error("resolution exceeds prototype budget");
            auto frame = std::make_shared<Frame>();
            frame->width = raw->width; frame->height = raw->height;
            auto pts = raw->best_effort_timestamp;
            if (pts != AV_NOPTS_VALUE) {
                if (first == AV_NOPTS_VALUE) first = pts;
                frame->pts = av_rescale_q(pts - first, track->time_base, AVRational{1,1000000000});
            } else frame->pts = last_end;
            if (frame->pts <= previous) frame->pts = last_end;
            frame->duration = raw->duration > 0 ? av_rescale_q(raw->duration, track->time_base, AVRational{1,1000000000}) : fallback;
            frame->duration = std::max<int64_t>(1, frame->duration);
            previous = frame->pts; last_end = frame->pts + frame->duration;
            frame->rgba.resize(size_t(frame->width) * frame->height * 4);
            scale = sws_getCachedContext(scale, raw->width, raw->height, static_cast<AVPixelFormat>(raw->format),
                raw->width, raw->height, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
            if (!scale) throw std::runtime_error("scale");
            uint8_t *planes[4] = {frame->rgba.data(), nullptr, nullptr, nullptr};
            int strides[4] = {frame->width * 4, 0, 0, 0};
            if (sws_scale(scale, raw->data, raw->linesize, 0, raw->height, planes, strides) != raw->height)
                throw std::runtime_error("conversion");
            av_frame_unref(raw);
            // One-frame look-ahead: the next PTS defines how long this image
            // remains valid, even if a VFR container omitted/guessed duration.
            if (pending_frame) {
                pending_frame->duration = frame->pts - pending_frame->pts;
                publish(std::move(pending_frame));
            }
            pending_frame = std::move(frame);
        }
    } catch (...) { bad = true; }
    if (pending_frame && !stop_ && generation_ == generation) publish(std::move(pending_frame));
    sws_freeContext(scale); av_frame_free(&raw); av_packet_free(&packet);
    avcodec_free_context(&codec); avformat_close_input(&format);
    if (generation_ == generation) { job->failed = bad; job->eof = true; if(bad && changed_) changed_(); }
}
}
