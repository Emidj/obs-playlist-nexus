#include <obs.h>
#include <util/base.h>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <thread>
#include "continuity_probe.hpp"
#include <windows.h>
#include <psapi.h>
#include <map>
#include <set>

struct Probe {
    std::atomic<unsigned> transitions{0}, failures{0}, frames{0}, black{0}, changes{0}, callbacks{0};
    std::atomic<bool> started{false};
    int last_color = -1;
};
static Probe probe;
static ContinuityProbe identity;
static bool real_media=false;
static std::ofstream real_trace;
static std::ofstream recording;
static uint64_t previous_hash=0, repeated=0, longest_repeat=0;
static std::atomic<bool> exhausted{false};
static bool balanced_test=false;
static std::vector<int> observed_clips;
static void logger(int level, const char *format, va_list arguments, void *) {
    char message[4096]; vsnprintf(message, sizeof(message), format, arguments);
    unsigned long long transitions = 0, failures = 0;
    if (sscanf(message, "[Playlist NEXUS] transition=%llu continuity_failures=%llu", &transitions, &failures) == 2) {
        probe.transitions = unsigned(transitions); probe.failures = unsigned(failures);
    }
    if (level <= LOG_WARNING) std::cerr << message << '\n';
    if(strstr(message,"No usable clip")) exhausted=true;
    if (strstr(message, "First texture")) std::cout << message << std::endl;
}
static void capture(void *, video_data *frame) {
    ++probe.callbacks;
    if(!identity.fixtures.empty()) identity.frame(frame->data[0],frame->linesize[0],int64_t(frame->timestamp));
    if(balanced_test&&identity.started&&(observed_clips.empty()||observed_clips.back()!=identity.last_clip))observed_clips.push_back(identity.last_clip);
    if(recording.is_open() && identity.started) {
        std::vector<uint8_t> rgb(640*360*3);
        for(int y=0;y<360;++y) for(int x=0;x<640;++x) {
            const auto *p=frame->data[0]+(y*3)*frame->linesize[0]+x*3*4;
            for(int c=0;c<3;++c) rgb[(y*640+x)*3+c]=p[c];
        }
        recording.write(reinterpret_cast<const char *>(rgb.data()),rgb.size());
    }
    if(real_media) {
        uint64_t hash=1469598103934665603ull; unsigned sum=0;
        for(int y=30;y<1080;y+=60) for(int x=30;x<1920;x+=60) {
            const auto *p=frame->data[0]+y*frame->linesize[0]+x*4;
            for(int c=0;c<3;++c) { hash=(hash^p[c])*1099511628211ull; sum+=p[c]; }
        }
        if(sum>32*18*3*3) probe.started=true;
        if(probe.started) {
            ++probe.frames; if(sum<=32*18*3*3) ++probe.black;
            repeated=hash==previous_hash ? repeated+1 : 1; previous_hash=hash;
            longest_repeat=std::max(longest_repeat,repeated);
            real_trace<<frame->timestamp<<','<<hash<<','<<repeated<<','<<sum<<'\n';
        }
        return;
    }
    const auto *pixel = frame->data[0] + 540*frame->linesize[0] + 960*4;
    const bool black = int(pixel[0])+int(pixel[1])+int(pixel[2]) < 50;
    if (!black) probe.started = true;
    if (!probe.started) return; // Initial source preparation is measured separately.
    ++probe.frames;
    if (black) ++probe.black;
    int color = pixel[0] >= pixel[1] && pixel[0] >= pixel[2] ? 0 : pixel[1] >= pixel[2] ? 1 : 2;
    if (!black && color != probe.last_color) { ++probe.changes; probe.last_color = color; }
}
int main(int argc, char **argv) {
    const auto wall_start=std::chrono::steady_clock::now();
    if (argc < 6) { std::cerr << "usage: obs_render_tests obs-bin plugin.dll red green blue [random]\n"; return 2; }
    base_set_log_handler(logger, nullptr);
    if(const auto *output=std::getenv("NEXUS_RECORD_RGB")) recording.open(output,std::ios::binary);
    if(const auto *manifest=std::getenv("NEXUS_ID_MANIFEST")) {
        identity.load(manifest,std::getenv("NEXUS_CAPTURE_CSV") ? std::getenv("NEXUS_CAPTURE_CSV") : "capture.csv");
    }
    std::vector<std::string> paths;
    real_media=std::getenv("NEXUS_REAL_PLAYLIST")!=nullptr;
    if(real_media) {
        std::ifstream list(std::getenv("NEXUS_REAL_PLAYLIST")); std::string line;
        while(std::getline(list,line)) { if(!line.empty()&&line.back()=='\r') line.pop_back(); if(!line.empty()) paths.push_back(line); }
        if(paths.empty()) return 8;
        real_trace.open(std::getenv("NEXUS_CAPTURE_CSV") ? std::getenv("NEXUS_CAPTURE_CSV") : "real-capture.csv");
        real_trace<<"timestamp_ns,sampled_hash,repeated_run,brightness_sum\n";
    } else if(!identity.fixtures.empty()) {
        for(auto &f:identity.fixtures) paths.push_back(f.path);
    } else for(int i=3;i<=5;++i) paths.push_back(argv[i]);
    const auto bin = std::filesystem::u8path(argv[1]);
    obs_add_data_path(((bin / "../../data/libobs").generic_string() + "/").c_str());
    if (!obs_startup("en-US", nullptr, nullptr)) return 3;
    std::cout << "libobs=" << obs_get_version_string() << std::endl;
    obs_video_info video{};
    auto graphics = (bin / "libobs-d3d11.dll").string();
    video.graphics_module = graphics.c_str(); video.fps_num = 60; video.fps_den = 1;
    video.base_width = video.output_width = 1920; video.base_height = video.output_height = 1080;
    video.output_format = VIDEO_FORMAT_RGBA; video.colorspace = VIDEO_CS_709;
    video.range = VIDEO_RANGE_FULL; video.scale_type = OBS_SCALE_BILINEAR;
    const int result = obs_reset_video(&video);
    if (result != OBS_VIDEO_SUCCESS) { std::cerr << "reset video=" << result << '\n'; obs_shutdown(); return 4; }
    obs_module_t *module = nullptr;
    if (obs_open_module(&module, argv[2], ".") != MODULE_SUCCESS || !obs_init_module(module)) {
        std::cerr << "module load failed\n"; obs_shutdown(); return 5;
    }
    auto *settings = obs_data_create(); auto *files = obs_data_array_create();
    balanced_test=std::getenv("NEXUS_BALANCED_TEST")!=nullptr;
    for (const auto &file : paths) {
        auto *item = obs_data_create();
        auto path = std::filesystem::absolute(std::filesystem::u8path(file)).string();
        obs_data_set_string(item, "value", path.c_str()); obs_data_array_push_back(files, item); obs_data_release(item);
    }
    obs_data_set_array(settings, "files", files); obs_data_array_release(files);
    obs_data_set_bool(settings, "random", argc > 6);
    if(balanced_test){
        obs_data_set_int(settings,"order_mode",2);obs_data_set_int(settings,"group_by",3);
        auto *grouped=obs_data_get_array(settings,"files");
        for(size_t i=0;i<obs_data_array_count(grouped);++i){auto *item=obs_data_array_item(grouped,i);auto label=std::to_string(i<2?0:i-1);obs_data_set_string(item,"manual_group",label.c_str());obs_data_release(item);}
        obs_data_set_array(settings,"files",grouped);obs_data_array_release(grouped);
    }
    auto *source = obs_source_create("obs_playlist_nexus", "NEXUS render test", settings, nullptr);
    obs_data_release(settings);
    if (!source) { obs_shutdown(); return 6; }
    obs_set_output_source(0, source);
    video_scale_info conversion{}; conversion.format = VIDEO_FORMAT_RGBA;
    conversion.width = 1920; conversion.height = 1080;
    conversion.range = VIDEO_RANGE_FULL; conversion.colorspace = VIDEO_CS_709;
    obs_add_raw_video_callback(&conversion, capture, nullptr);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(std::getenv("NEXUS_TEST_TIMEOUT") ? std::atoi(std::getenv("NEXUS_TEST_TIMEOUT")) : 60);
    std::ofstream memory_trace;
    if(const auto *path=std::getenv("NEXUS_MEMORY_CSV")) {
        memory_trace.open(path); memory_trace<<"seconds,working_set_mib,private_commit_mib,handles\n";
    }
    auto next_memory=std::chrono::steady_clock::now();
    unsigned target = std::getenv("NEXUS_TEST_TRANSITIONS") ? unsigned(std::atoi(std::getenv("NEXUS_TEST_TRANSITIONS"))) : 100;
    const bool soak=real_media&&std::getenv("NEXUS_SOAK")!=nullptr;
    while ((soak||probe.transitions < target) && !exhausted && std::chrono::steady_clock::now() < deadline) {
        if(memory_trace.is_open() && std::chrono::steady_clock::now()>=next_memory) {
            PROCESS_MEMORY_COUNTERS_EX sample{}; sample.cb=sizeof(sample); DWORD handles=0;
            GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&sample),sizeof(sample));
            GetProcessHandleCount(GetCurrentProcess(),&handles);
            memory_trace<<std::chrono::duration<double>(std::chrono::steady_clock::now()-wall_start).count()<<','
                <<sample.WorkingSetSize/(1024.*1024.)<<','<<sample.PrivateUsage/(1024.*1024.)<<','<<handles<<'\n';
            next_memory+=std::chrono::seconds(1);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    // Let captured output drain past the last logged switch.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    obs_remove_raw_video_callback(capture, nullptr);
    obs_set_output_source(0, nullptr); obs_source_release(source);
    std::cout << "mode=" << (balanced_test ? "balanced" : argc > 6 ? "random" : "sequential") << " transitions=" << probe.transitions
        << " failures=" << probe.failures << " frames=" << probe.frames << " black=" << probe.black
        << " observed_color_runs=" << probe.changes << std::endl;
    std::cout << "capture_callbacks=" << probe.callbacks << std::endl;
    bool pass = probe.transitions >= target && !probe.failures && probe.frames > target && !probe.black && probe.changes >= target;
    if(!identity.fixtures.empty()) {
        std::cout<<"identity_valid="<<identity.valid<<" invalid="<<identity.invalid<<" switches="<<identity.switches
            <<" timing_errors="<<identity.timing_errors<<" timestamp_gaps="<<identity.timestamp_gaps<<" backwards="<<identity.backwards<<std::endl;
        std::cout<<"skipped="<<identity.skipped<<" excess_repeats="<<identity.excess_repeats<<" endpoint_errors="<<identity.endpoint_errors<<std::endl;
        pass=identity.passed(target)&&probe.transitions>=target&&!probe.failures;
    }
    if(real_media) {
        std::cout<<"real_playlist="<<paths.size()<<" longest_identical_sample_run="<<longest_repeat<<std::endl;
        pass=probe.transitions>=target&&!probe.failures&&probe.frames>target&&!exhausted&&
            (!soak||std::chrono::steady_clock::now()>=deadline);
    }
    if(balanced_test){
        std::map<int,int> group_by_id;std::map<int,size_t> sizes;std::map<int,std::set<int>> bags;std::set<int> round;
        for(size_t i=0;i<identity.fixtures.size();++i){int group=int(i<2?0:i-1);group_by_id[identity.fixtures[i].id]=group;++sizes[group];}
        int previous=-1;unsigned errors=0;
        for(auto id:observed_clips){int group=group_by_id.at(id);if(group==previous&&sizes.size()>1)++errors;if(!round.insert(group).second)++errors;
            if(!bags[group].insert(id).second)++errors;if(bags[group].size()==sizes[group])bags[group].clear();
            if(round.size()==sizes.size())round.clear();previous=group;
        }
        std::cout<<"balanced_clips="<<observed_clips.size()<<" balanced_errors="<<errors<<std::endl;
        pass=pass&&errors==0&&observed_clips.size()>=target;
    }
    if(std::getenv("NEXUS_EXPECT_EXHAUSTED")) pass=exhausted&&probe.transitions==0;
    PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb=sizeof(memory);
    GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memory),sizeof(memory));
    FILETIME created{},exited{},kernel{},user{};
    GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user);
    auto ticks=[](FILETIME t) { return (uint64_t(t.dwHighDateTime)<<32)|t.dwLowDateTime; };
    const double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-wall_start).count();
    std::cout<<"wall_seconds="<<elapsed<<" cpu_seconds="<<(ticks(kernel)+ticks(user))/1e7
        <<" cpu_percent_one_core="<<(ticks(kernel)+ticks(user))/1e5/elapsed
        <<" peak_working_set_mib="<<memory.PeakWorkingSetSize/(1024.*1024.)
        <<" peak_commit_mib="<<memory.PeakPagefileUsage/(1024.*1024.)<<std::endl;
    std::cout<<"obs_average_frame_ms="<<obs_get_average_frame_time_ns()/1e6
        <<" obs_total_frames="<<obs_get_total_frames()<<" obs_lagged_frames="<<obs_get_lagged_frames()<<std::endl;
    obs_shutdown(); return pass ? 0 : 1;
}
