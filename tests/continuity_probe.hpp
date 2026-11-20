#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>
#include <stdexcept>
struct Fixture {
    int id=0,w=0,h=0,fps=0,count=0;
    std::string path;
    std::vector<int64_t> pts, ends;
    int64_t begin(int frame) const { return pts.empty() ? std::llround(double(frame)*1e9/fps) : pts.at(frame); }
    int64_t end(int frame) const { return ends.empty() ? std::llround(double(frame+1)*1e9/fps) : ends.at(frame); }
    int64_t duration() const { return end(count-1); }
    uint64_t max_repeats(int frame) const { return uint64_t((end(frame)-begin(frame)+16666666)/16666667); }
};
struct ContinuityProbe {
    std::vector<Fixture> fixtures;
    std::ofstream trace;
    int64_t sequence_start=0, origin_low=0, origin_high=0, last_timestamp=0;
    int last_clip=-1, last_frame=-1;
    uint64_t valid=0, invalid=0, timing_errors=0, timestamp_gaps=0, backwards=0, switches=0;
    uint64_t skipped=0, excess_repeats=0, endpoint_errors=0, run=0;
    bool started=false;
    void load(const std::string &manifest, const std::string &csv) {
        std::ifstream input(manifest); std::string line;
        while(std::getline(input,line)) {
            std::stringstream row(line); Fixture f; std::string value;
            int *fields[]={&f.id,&f.w,&f.h,&f.fps,&f.count};
            for(auto field:fields) { std::getline(row,value,'\t'); *field=std::stoi(value); }
            std::getline(row,f.path); if(!f.path.empty() && f.path.back()=='\r') f.path.pop_back();
            // Optional independently demuxed timing oracle (ffprobe stream ticks).
            std::ifstream timing(f.path+".timing.tsv"); std::string sample;
            while(std::getline(timing,sample)) {
                std::stringstream clock(sample); int64_t begin=0,end=0; clock>>begin>>end;
                if(!clock || end<=begin) throw std::runtime_error("invalid timing oracle");
                f.pts.push_back(begin); f.ends.push_back(end);
            }
            if(!f.pts.empty() && f.pts.size()!=size_t(f.count)) throw std::runtime_error("timing count mismatch");
            fixtures.push_back(std::move(f));
        }
        if(fixtures.empty()) throw std::runtime_error("empty frame-ID manifest");
        trace.open(csv); if(!trace) throw std::runtime_error("cannot open capture trace");
        trace << "timestamp_ns,clip,frame,valid,origin_low_ns,origin_high_ns\n";
    }
    const Fixture *find(int id) const { for(auto &f:fixtures) if(f.id==id) return &f; return nullptr; }
    void frame(const uint8_t *rgba, uint32_t stride, int64_t timestamp) {
        const Fixture *matched=nullptr; int original=-1;
        for(const auto &f:fixtures) {
            const double scale=std::min(1920.0/f.w,1080.0/f.h), unit=std::min(f.w,f.h)*scale;
            uint32_t words[2]={0,0}; bool robust=true;
            for(int row=0;row<2;++row) for(int bit=0;bit<24;++bit) {
                int x=int(960-unit*.4+(bit+.5)*unit*.8/24), y=int(540+(row ? .04 : -.04)*unit);
                const auto *p=rgba+y*stride+x*4;
                const int luminance=(int(p[0])+p[1]+p[2])/3;
                if(luminance>70 && luminance<180) robust=false;
                words[row]=(words[row]<<1)|(luminance>=128);
            }
            if(robust && words[0]>>16==0xA5 && (words[0]^words[1])==0xFFFFFF && int((words[0]>>12)&15)==f.id && int(words[0]&4095)<f.count) {
                matched=&f; original=int(words[0]&4095); break;
            }
        }
        if(!matched) {
            if(started) { ++invalid; trace<<timestamp<<",-1,-1,0,0,0\n"; }
            return;
        }
        if(started && std::abs((timestamp-last_timestamp)-16666667)>1000000) ++timestamp_gaps;
        last_timestamp=timestamp;
        if(started && matched->id!=last_clip) {
            if(last_frame!=find(last_clip)->count-1 || original!=0) ++endpoint_errors;
            sequence_start+=find(last_clip)->duration(); ++switches;
            run=1;
        } else if(started) {
            if(original<last_frame) ++backwards;
            if(original>last_frame+1) ++skipped;
            run=original==last_frame ? run+1 : 1;
            if(run>matched->max_repeats(original)) ++excess_repeats;
        } else run=1;
        const int64_t begin=sequence_start+matched->begin(original);
        const int64_t end=sequence_start+matched->end(original);
        // A single fixed phase must explain the entire capture. A 2 us allowance
        // covers integer timestamp rounding, not an extra 16.7 ms output frame.
        const auto low=timestamp-end-2000, high=timestamp-begin+2000;
        if(!started) { origin_low=low; origin_high=high; }
        else {
            origin_low=std::max(origin_low,low); origin_high=std::min(origin_high,high);
            if(origin_low>origin_high) ++timing_errors;
        }
        started=true; last_clip=matched->id; last_frame=original; ++valid;
        trace<<timestamp<<','<<matched->id<<','<<original<<",1,"<<origin_low<<','<<origin_high<<'\n';
    }
    bool passed(unsigned target) const { return valid>target && switches>=target && !invalid && !timing_errors && !timestamp_gaps && !backwards && !skipped && !excess_repeats && !endpoint_errors; }
};
