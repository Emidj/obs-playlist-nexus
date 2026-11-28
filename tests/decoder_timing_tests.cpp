#include "decoder.hpp"
#include "continuity_probe.hpp"
#include <chrono>
#include <iostream>
#include <thread>
int main(int argc,char **argv) {
    try {
        if(argc!=3) throw std::runtime_error("usage: decoder_timing_tests manifest trace.csv");
        ContinuityProbe oracle; oracle.load(argv[1],argv[2]);
        oracle.trace.close();
        std::ofstream observed(argv[2]);
        observed<<"clip,frame,observed_pts_ns,expected_pts_ns,observed_duration_ns,expected_duration_ns\n";
        for(const auto &fixture:oracle.fixtures) {
            if(fixture.pts.empty()) throw std::runtime_error("independent timing oracle required");
            nexus::Decoder decoder; decoder.request(fixture.path);
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
            for(int i=0;i<fixture.count;++i) {
                nexus::Sample sample;
                do {
                    sample=decoder.sample(fixture.begin(i)+1000);
                    if(sample.failed) throw std::runtime_error("fixture decode failed");
                    if(sample.frame && sample.frame->pts>=fixture.begin(i)-2) break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                } while(std::chrono::steady_clock::now()<deadline);
                if(sample.frame) observed<<fixture.id<<','<<i<<','<<sample.frame->pts<<','<<fixture.begin(i)<<','
                    <<sample.frame->duration<<','<<fixture.end(i)-fixture.begin(i)<<'\n';
                if(!sample.frame || std::abs(sample.frame->pts-fixture.begin(i))>2 ||
                   std::abs(sample.frame->duration-(fixture.end(i)-fixture.begin(i)))>2)
                    throw std::runtime_error("PTS/duration mismatch at clip "+std::to_string(fixture.id)+" frame "+std::to_string(i));
            }
            std::cout<<"PASS clip="<<fixture.id<<" frames="<<fixture.count<<" exact_pts_and_durations\n";
        }
    } catch(const std::exception &error) { std::cerr<<error.what()<<'\n'; return 1; }
}
