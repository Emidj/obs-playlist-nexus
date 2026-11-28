#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
int main(int argc, char **argv) {
    if (argc != 7) { std::cerr << "usage: fixture_generator file.rgb clip_id frames width height fps\n"; return 1; }
    const int id=std::stoi(argv[2]), count=std::stoi(argv[3]), w=std::stoi(argv[4]), h=std::stoi(argv[5]);
    if (id < 1 || id > 15 || count < 1 || count > 4096 || w < 240 || h < 240) return 2;
    std::ofstream output(argv[1], std::ios::binary);
    const int unit=std::min(w,h);
    const char *digits[]={"111101101101111","010110010010111","111001111100111","111001111001111","101101111001001","111100111001111","111100111101111","111001001001001","111101111101111","111101111001111"};
    for (int frame=0; frame<count; ++frame) {
        std::vector<uint8_t> pixels(size_t(w)*h*3);
        for (size_t i=0;i<pixels.size();i+=3) { pixels[i]=uint8_t(30+id*20); pixels[i+1]=30; pixels[i+2]=70; }
        auto box=[&](int x,int y,int bw,int bh,uint8_t value) {
            for(int yy=std::max(0,y);yy<std::min(h,y+bh);++yy)
                for(int xx=std::max(0,x);xx<std::min(w,x+bw);++xx)
                    for(int c=0;c<3;++c) pixels[(size_t(yy)*w+xx)*3+c]=value;
        };
        const uint32_t word=(0xA5u<<16)|(unsigned(id)<<12)|unsigned(frame);
        for(int row=0;row<2;++row) for(int bit=0;bit<24;++bit) {
            const int left=int(w*.5-unit*.4+bit*unit*.8/24);
            const int right=int(w*.5-unit*.4+(bit+1)*unit*.8/24);
            const int y=int(h*.5+(row ? .04 : -.04)*unit);
            const bool on=bool((word>>(23-bit))&1u) != bool(row);
            box(left,y-int(unit*.015),right-left,std::max(2,int(unit*.03)),on ? 235 : 16);
        }
        // Large human-readable clip ID and four-digit original frame number.
        const std::string label=std::to_string(id)+" "+std::string(4-std::to_string(frame).size(),'0')+std::to_string(frame);
        const int cell=std::max(2,unit/70), start=w/2-int(label.size())*cell*2;
        for(size_t n=0;n<label.size();++n) if(label[n]!=' ')
            for(int y=0;y<5;++y) for(int x=0;x<3;++x)
                if(digits[label[n]-'0'][y*3+x]=='1') box(start+int(n)*cell*4+x*cell,h/5+y*cell,cell,cell,235);
        output.write(reinterpret_cast<const char *>(pixels.data()),std::streamsize(pixels.size()));
    }
    return output ? 0 : 3;
}
