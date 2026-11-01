#include "RoadLimits.h"
#include "PackReader.h"
#include <LittleFS.h>
#include <time.h>
namespace {
struct FlashSource {
    File file;
    size_t size() { return file ? file.size() : 0; }
    bool readAt(uint32_t at, uint8_t* out, size_t n) { return file && file.seek(at) && file.read(out,n)==n; }
} source;
PackReader<FlashSource> reader(source);
bool ready=false;
}
bool RoadLimits::begin() {
    _version[0]=0;
    ready=false;
    if (!LittleFS.begin(false)) { Serial.println("[W][ROAD] LittleFS unavailable"); return false; }
    source.file=LittleFS.open("/limits.bin","r");
    ready=reader.begin();
    if (!ready) { Serial.println("[W][ROAD] Missing or invalid NZSL pack"); return false; }
    time_t t=source.file.getLastWrite();
    if (t>0) { struct tm utc; gmtime_r(&t,&utc); strftime(_version,sizeof(_version),"%Y-%m-%d",&utc); }
    return true;
}
int RoadLimits::lookup(double lat,double lon,float heading,float speed) { return ready ? reader.lookup(lat,lon,heading,speed) : 0; }
