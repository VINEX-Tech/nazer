#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

// Source supplies size() and readAt(offset, destination, length). Both the
// flash reader and host checks use this parser; no complete pack is held in RAM.
template<class Source> class PackReader {
    Source& src;
    uint16_t cell=0, rows=0, cols=0;
    int32_t minLat=0, minLon=0;
    uint32_t dataStart=0, cached=UINT32_MAX, cacheStart=0;
    std::vector<uint8_t> cache;
    bool valid=false;
    static uint16_t u16(const uint8_t* p) { return p[0] | uint16_t(p[1])<<8; }
    static uint32_t u32(const uint8_t* p) { return u16(p) | uint32_t(u16(p+2))<<16; }
    bool read(uint32_t pos, uint8_t* p, size_t n) {
        if (pos >= cacheStart && uint64_t(pos)+n <= uint64_t(cacheStart)+cache.size()) {
            memcpy(p, cache.data()+pos-cacheStart, n); return true;
        }
        return uint64_t(pos)+n <= src.size() && src.readAt(pos,p,n);
    }
    bool offsets(uint32_t i, uint32_t& a, uint32_t& b) {
        uint8_t v[8];
        if (!src.readAt(20+i*4,v,8)) return false;
        a=u32(v); b=u32(v+4);
        return b>=a+2 && uint64_t(dataStart)+b<=src.size();
    }
public:
    explicit PackReader(Source& s):src(s) {}
    bool begin() {
        valid=false; cache.clear(); cached=UINT32_MAX;
        uint8_t h[20];
        if (src.size()<24 || !src.readAt(0,h,20) || memcmp(h,"NZSL",4) || u16(h+4)!=1) return false;
        cell=u16(h+6); minLat=int32_t(u32(h+8)); minLon=int32_t(u32(h+12)); rows=u16(h+16); cols=u16(h+18);
        uint64_t count=uint64_t(rows)*cols;
        if (!cell || !count || 20+(count+1)*4>src.size()) return false;
        dataStart=20+(count+1)*4;
        uint32_t a,b;
        for (uint32_t i=0;i<count;++i) {
            if (!offsets(i,a,b) || (i==0 && a!=0)) return false;
            uint32_t pos=dataStart+a, end=dataStart+b;
            uint8_t v[11];
            if (!read(pos,v,2)) return false;
            uint16_t segments=u16(v); pos+=2;
            for (uint16_t j=0;j<segments;++j) {
                if (end-pos<11 || !read(pos,v,11) || !v[0] || v[2]<2) return false;
                uint32_t bytes=11+4*(v[2]-1);
                if (bytes>end-pos) return false;
                pos+=bytes;
            }
            if (pos!=end) return false;
        }
        valid=true; return true;
    }
    int lookup(double lat, double lon, float heading, float speed) {
        if (!valid || !std::isfinite(lat) || !std::isfinite(lon) || lat < -90 || lat > 90 || lon < -180 || lon > 180) return 0;
        int r=int(std::floor((lat*1e5-minLat)/cell)), c=int(std::floor((lon*1e5-minLon)/cell));
        if (r < -1 || c < -1 || r > rows || c > cols) return 0;
        double best=25.000001; int limit=0; bool bestAligned=false;
        const double k=111320., cosine=std::cos(lat*3.141592653589793/180.);
        for (int dr=-1;dr<=1;++dr) for (int dc=-1;dc<=1;++dc) {
            int rr=r+dr, cc=c+dc;
            if (rr<0 || cc<0 || rr>=rows || cc>=cols) continue;
            uint32_t id=rr*cols+cc,a,b;
            if (!offsets(id,a,b)) return 0;
            // Cache the current cell. Large cells stream through the same reader.
            if (!dr && !dc && cached!=id) {
                cache.clear(); cached=id; cacheStart=dataStart+a;
                if (b-a<=8192) {
                    cache.resize(b-a);
                    if (!src.readAt(cacheStart,cache.data(),cache.size())) {cache.clear(); return 0;}
                }
            }
            uint32_t pos=dataStart+a, end=dataStart+b; uint8_t v[11];
            if (!read(pos,v,2)) return 0;
            uint16_t n=u16(v); pos+=2;
            for (uint16_t s=0;s<n;++s) {
                if (end-pos<11 || !read(pos,v,11)) return 0;
                int kmh=v[0], points=v[2];
                if (points<2 || !kmh || uint64_t(pos)+11+4*(points-1)>end) return 0;
                int64_t la=int32_t(u32(v+3)),lo=int32_t(u32(v+7)); pos+=11;
                for (int j=1;j<points;++j) {
                    if (!read(pos,v,4)) return 0;
                    pos+=4;
                    int64_t lb=la+int16_t(u16(v)),lob=lo+int16_t(u16(v+2));
                    double ax=(lo*1e-5-lon)*k*cosine, ay=(la*1e-5-lat)*k;
                    double dx=(lob-lo)*1e-5*k*cosine,dy=(lb-la)*1e-5*k;
                    double length=dx*dx+dy*dy;
                    double t=length ? -(ax*dx+ay*dy)/length : 0;
                    t=std::fmax(0.,std::fmin(1.,t));
                    double distance=std::hypot(ax+t*dx,ay+t*dy);
                    double bearing=std::atan2(dx,dy)*180./3.141592653589793;
                    double diff=std::fmod(std::fabs(bearing-heading),180.);
                    bool aligned=speed>10 && std::isfinite(heading) && (diff<=45 || diff>=135);
                    if (distance<=25 && ((!limit) || (aligned && !bestAligned) || (aligned==bestAligned && distance<best))) {
                        best=distance; bestAligned=aligned; limit=kmh;
                    }
                    la=lb;lo=lob;
                }
            }
            if (pos!=end) return 0;
        }
        return limit;
    }
};
