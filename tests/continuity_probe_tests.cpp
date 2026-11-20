#include "continuity_probe.hpp"
#include <cstdlib>
#include <iostream>
static void require(bool v,const char *message) { if(!v) { std::cerr<<message<<'\n'; std::exit(1); } }
static void emit(ContinuityProbe &probe,int number,int64_t timestamp) {
    std::vector<uint8_t> rgba(1920*1080*4);
    const unsigned word=(0xA5u<<16)|(1u<<12)|unsigned(number);
    for(int row=0;row<2;++row) for(int bit=0;bit<24;++bit) {
        int x=int(960-1080*.4+(bit+.5)*1080*.8/24),y=int(540+(row ? .04 : -.04)*1080);
        const uint8_t value=bool((word>>(23-bit))&1u)!=bool(row) ? 235 : 16;
        for(int c=0;c<3;++c) rgba[(y*1920+x)*4+c]=value;
    }
    probe.frame(rgba.data(),1920*4,timestamp);
}
static ContinuityProbe make() { ContinuityProbe p; p.fixtures.push_back({1,1920,1080,60,100,"test"}); return p; }
int main() {
    auto good=make(); for(int n=0;n<10;++n) emit(good,n,int64_t(n)*16666667);
    require(good.passed(0),"valid 60 FPS sequence rejected");
    auto frozen=make(); for(int n=0;n<10;++n) emit(frozen,n==2 ? 1 : n,int64_t(n)*16666667);
    require(!frozen.passed(0) && frozen.excess_repeats && frozen.skipped,"repeat/skip went undetected");
    auto missing=make(); emit(missing,0,0); emit(missing,1,33333334);
    require(!missing.passed(0)&&missing.timestamp_gaps,"capture gap went undetected");
    auto invalid=make(); emit(invalid,0,0); std::vector<uint8_t> black(1920*1080*4); invalid.frame(black.data(),1920*4,16666667);
    require(!invalid.passed(0)&&invalid.invalid,"invalid marker went undetected");
    auto vfr=make();
    vfr.fixtures[0].pts={0,33333333,133333333,166666667};
    vfr.fixtures[0].ends={33333333,133333333,166666667,200000000};
    vfr.fixtures[0].count=4;
    for(int n=0;n<12;++n) {
        int frame=n<2 ? 0 : n<8 ? 1 : n<10 ? 2 : 3;
        emit(vfr,frame,int64_t(n)*16666667);
    }
    require(vfr.passed(0),"legitimate long VFR hold rejected");
    std::cout<<"PASS: baseline, deliberate freeze/skip, missing capture and invalid marker\n";
}
