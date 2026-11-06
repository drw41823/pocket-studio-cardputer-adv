#pragma once
#include <cstdint>
#include <cstring>
#include <algorithm>

namespace pocket {
using ReadAt = bool (*)(void*, uint32_t, void*, unsigned);
struct WavInfo { uint32_t rate=0, frames=0, offset=0, bytes=0; uint16_t channels=0; };
enum class WavError { None, Read, Container, Format, Truncated, TooLong };
inline const char* wavError(WavError e) {
    switch(e) {
    case WavError::Read: return "WAV read failed";
    case WavError::Container: return "Use RIFF/WAVE PCM";
    case WavError::Format: return "Use 16bit PCM 8-48k mono/stereo";
    case WavError::Truncated: return "WAV is incomplete";
    case WavError::TooLong: return "Max 32768 frames - trim WAV on PC";
    default: return "WAV ready";
    }
}
inline uint16_t wav16(const uint8_t* p) { return uint16_t(p[0]) | uint16_t(p[1])<<8; }
inline uint32_t wav32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1])<<8 | uint32_t(p[2])<<16 | uint32_t(p[3])<<24; }
inline WavError inspectWav(ReadAt read, void* context, uint32_t size, WavInfo& out, uint32_t maxFrames=32768) {
    uint8_t h[24]{};
    if(size<12 || !read(context,0,h,12)) return WavError::Read;
    if(std::memcmp(h,"RIFF",4) || std::memcmp(h+8,"WAVE",4)) return WavError::Container;
    uint64_t limit=uint64_t(wav32(h+4))+8;
    if(limit>size || limit<12) return WavError::Truncated;
    WavInfo info; bool fmt=false, data=false;
    unsigned chunks=0;
    for(uint64_t at=12;at+8<=limit;) {
        if(++chunks>256) return WavError::Format;
        if(!read(context,uint32_t(at),h,8)) return WavError::Read;
        uint32_t bytes=wav32(h+4); uint64_t end=at+8+bytes;
        if(end>limit) return WavError::Truncated;
        if(!std::memcmp(h,"fmt ",4)) {
            if(fmt || bytes<16 || !read(context,uint32_t(at+8),h,16)) return WavError::Format;
            info.channels=wav16(h+2); info.rate=wav32(h+4);
            if(wav16(h)!=1 || (info.channels!=1 && info.channels!=2) || wav16(h+14)!=16
                || info.rate<8000 || info.rate>48000 || wav16(h+12)!=info.channels*2
                || wav32(h+8)!=info.rate*info.channels*2) return WavError::Format;
            fmt=true;
        } else if(!std::memcmp(h,"data",4)) {
            if(data) return WavError::Format;
            info.offset=uint32_t(at+8); info.bytes=bytes; data=true;
        }
        at=end+(bytes&1u);
        if(at>limit) return WavError::Truncated;
    }
    if(!fmt || !data || !info.bytes || info.bytes%(info.channels*2)) return WavError::Format;
    info.frames=info.bytes/(info.channels*2);
    if(info.frames<32) return WavError::Format;
    if(info.frames>maxFrames) return WavError::TooLong;
    out=info; return WavError::None;
}
// A long recording stays on SD. Only an explicitly selected, bounded region
// becomes a RAM instrument; do not relax the ordinary instrument parser limit.
inline bool wavWindow(const WavInfo& source,uint32_t start,uint32_t end,WavInfo& out) {
    if(start>=end||end>source.frames||end-start<32||end-start>32768||!source.channels||source.channels>2)return false;
    uint64_t offset=uint64_t(source.offset)+uint64_t(start)*source.channels*2;
    if(offset>0xffffffffu)return false;
    out=source;out.offset=uint32_t(offset);out.frames=end-start;out.bytes=out.frames*out.channels*2;return true;
}
inline bool decodeWav(ReadAt read,void* context,const WavInfo& info,int16_t* out,unsigned capacity) {
    if(!out || info.frames>capacity || !info.channels || info.channels>2) return false;
    uint8_t bytes[512];
    for(unsigned at=0;at<info.frames;) {
        unsigned n=std::min<unsigned>(info.frames-at,sizeof(bytes)/(info.channels*2));
        if(!read(context,info.offset+at*info.channels*2,bytes,n*info.channels*2)) return false;
        for(unsigned i=0;i<n;++i) {
            const auto* p=bytes+i*info.channels*2;
            int value=int16_t(wav16(p));
            if(info.channels==2) value=(value+int16_t(wav16(p+2)))/2;
            out[at+i]=int16_t(value);
        }
        at+=n;
    }
    return true;
}
}
