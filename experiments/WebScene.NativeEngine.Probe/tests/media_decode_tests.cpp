#include "decode_service.h"
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#if defined(__APPLE__)
#include <CoreVideo/CoreVideo.h>
#endif
using namespace webscene::media;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F function) { bool failed=false; try { function(); } catch(const std::exception&) { failed=true; } check(failed,"Expected decode rejection"); }
std::vector<uint8_t> wav() {
    std::vector<uint8_t> data(44 + 480 * 4);
    auto word=[&](size_t i,uint16_t v){data[i]=v;data[i+1]=v>>8;};
    auto dword=[&](size_t i,uint32_t v){word(i,v);word(i+2,v>>16);};
    memcpy(data.data(),"RIFF",4);dword(4,data.size()-8);memcpy(data.data()+8,"WAVEfmt ",8);
    dword(16,16);word(20,1);word(22,2);dword(24,48000);dword(28,192000);word(32,4);word(34,16);
    memcpy(data.data()+36,"data",4);dword(40,data.size()-44);
    for(size_t i=0;i<480;++i){word(44+i*4,16384);word(46+i*4,static_cast<uint16_t>(-8192));}
    return data;
}
std::shared_ptr<encoded_source> load(const char* path, const char* extension) {
    std::ifstream file(path,std::ios::binary);check(bool(file),"Fixture missing");
    auto source=std::make_shared<encoded_source>();source->extension=extension;
    source->bytes.assign(std::istreambuf_iterator<char>(file),{});return source;
}
int main(int argc,char** argv) {
    try {
        auto bytes=wav();auto result=decode_audio(bytes);
        check(result.channels==2&&result.sample_rate==48000&&result.frames()==480,"WAV format/frame count");
        for(size_t i=0;i<result.frames();++i){check(std::abs(result.samples[i*2]-.5f)<1e-6,"Left channel wrong");check(std::abs(result.samples[i*2+1]+.25f)<1e-6,"Right channel wrong");}
        rejects([&]{decode_audio({});});rejects([&]{decode_audio(std::vector<uint8_t>{1,2,3});});
        decode_limits small;small.decoded_audio_bytes=16;rejects([&]{decode_audio(bytes,small);});
        small={};small.encoded_bytes=4;rejects([&]{decode_audio(bytes,small);});
        std::stop_source cancelled;cancelled.request_stop();rejects([&]{decode_audio(bytes,{},cancelled.get_token());});
        std::future<audio_buffer> future;
        { decode_service service;auto source=std::make_shared<encoded_source>();source->bytes=bytes;future=service.audio(source);check(future.get().frames()==480,"Asynchronous decode failed");service.close();rejects([&]{service.audio(source);}); }
        // Service destruction settles pending work instead of stranding futures.
        {decode_service service;auto source=std::make_shared<encoded_source>();source->bytes=bytes;future=service.audio(source);}
        check(future.wait_for(std::chrono::seconds(0))==std::future_status::ready,"Future stranded by teardown");
        try{future.get();}catch(const std::exception&){}
        if(argc>1 && std::string(argv[1])!="-"){auto source=load(argv[1],".wav");decode_service service;auto audio=service.audio(source).get();check(audio.frames()>audio.sample_rate,"Demo score too short");double energy=0;for(float x:audio.samples){check(std::isfinite(x),"Nonfinite demo audio");energy+=x*x;}check(energy>1,"Silent demo audio");std::cout<<"Original score: "<<audio.frames()<<" frames, "<<audio.sample_rate<<" Hz, "<<audio.channels<<" channels\n";}
#if defined(__APPLE__)
        if(argc>2){auto source=load(argv[2],".mp4");video_frame first,later;
            {decode_service service;first=service.video(source,0).get();later=service.video(source,1).get();}
            check(first.native_surface&&later.native_surface&&first.width&&first.height,"No native video surface");
            check(later.timestamp>=.95&&later.timestamp>first.timestamp,"Seek did not advance decoded frame");
            auto checksum=[](const video_frame& f){auto pixel=static_cast<CVPixelBufferRef>(f.native_surface.get());CVPixelBufferLockBaseAddress(pixel,kCVPixelBufferLock_ReadOnly);auto p=static_cast<const uint8_t*>(CVPixelBufferGetBaseAddress(pixel));uint64_t hash=1469598103934665603ULL;for(size_t y=0;y<f.height;++y)for(size_t x=0;x<f.width*4;++x)hash=(hash^p[y*CVPixelBufferGetBytesPerRow(pixel)+x])*1099511628211ULL;CVPixelBufferUnlockBaseAddress(pixel,kCVPixelBufferLock_ReadOnly);return hash;};
            check(checksum(first)!=checksum(later),"Video frames did not change");
            check(CVPixelBufferGetIOSurface(static_cast<CVPixelBufferRef>(first.native_surface.get()))!=nullptr,"Missing shareable IOSurface");
            rejects([&]{decode_video_frame(*source,-1);});rejects([&]{decode_video_frame(*source,0,{},cancelled.get_token());});
            std::cout<<"Original video: "<<first.width<<"x"<<first.height<<", timestamps "<<first.timestamp<<" and "<<later.timestamp<<", retained IOSurface frames survive decoder teardown\n";
        }
#endif
        std::cout<<"Native media decode tests passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
