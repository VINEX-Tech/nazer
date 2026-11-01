#include "../../lib/RoadLimits/PackReader.h"
#include <fstream>
#include <cassert>
#include <iostream>
#include <vector>
struct Disk {
    std::ifstream f;
    size_t length;
    Disk(const char* path):f(path,std::ios::binary) { f.seekg(0,std::ios::end);length=f.tellg(); }
    size_t size() {return length;}
    bool readAt(uint32_t pos,uint8_t* out,size_t n) {f.clear(); f.seekg(pos); return bool(f.read(reinterpret_cast<char*>(out),n));}
};
int main() {
    Disk disk("test/host/limits.bin"); PackReader<Disk> p(disk); assert(p.begin());
    std::ifstream points("test/host/known_points.tsv");
    double lat,lon; int expected,count=0;
    while (points>>lat>>lon>>expected) { assert(p.lookup(lat,lon,0,80)==expected); ++count; }
    assert(count==3);
    assert(p.lookup(0,0,0,0)==0);
    assert(p.lookup(1e100,31,0,30)==0);
    assert(p.lookup(NAN,31,0,30)==0);
    Disk crossing("test/host/crossing.bin"); PackReader<Disk> other(crossing); assert(other.begin());
    assert(other.lookup(30.11,31.10,0,30)==100); // parallel road ~19 m away beats crossing at 0 m
    assert(other.lookup(30.11,31.10,180,30)==100); // either direction
    assert(other.lookup(30.11,31.10,90,30)==40);
    assert(other.lookup(30.11,31.10,0,10)==40); // nearest at low speed
    assert(other.lookup(30.10999,31.10,0,30)==100); // adjacent cell boundary
    struct Memory {
        std::vector<uint8_t> bytes;
        size_t size() { return bytes.size(); }
        bool readAt(uint32_t pos,uint8_t* out,size_t n) {
            if (uint64_t(pos)+n>bytes.size()) return false;
            memcpy(out,bytes.data()+pos,n); return true;
        }
    } bad;
    bad.bytes.resize(crossing.size()); assert(crossing.readAt(0,bad.bytes.data(),bad.bytes.size()));
    auto original=bad.bytes;
    bad.bytes[0]='X'; PackReader<Memory> invalid(bad); assert(!invalid.begin());
    bad.bytes=original; bad.bytes[4]=2; assert(!invalid.begin());
    bad.bytes=original; bad.bytes.resize(22); assert(!invalid.begin());
    bad.bytes=original; bad.bytes[20]=255; assert(!invalid.begin());
    bad.bytes=original;
    size_t dataStart=20+(size_t(bad.bytes[16] | bad.bytes[17]<<8)*(bad.bytes[18] | bad.bytes[19]<<8)+1)*4;
    bad.bytes[dataStart]=255; assert(!invalid.begin()); // malformed cell segment count
    std::cout<<"PASS: heading preference, reverse heading, slow nearest match, neighbour cell, five corrupt packs\n";
    std::cout<<"PASS: tagged motorway=120, estimated secondary=60, 200 m off-road=0, outside pack=0\n";
}
