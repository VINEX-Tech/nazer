#include "../../lib/Cellular/PointRing.h"
#include <cassert>
#include <iostream>
int main() {
    PointRing<int,120> points;
    for (int i=0;i<120;++i) assert(points.push(i));
    assert(!points.push(120));
    // A failed upload performs no acknowledgment.
    for (int i=0;i<10;++i) assert(points.at(i)==i);
    assert(points.size()==120);
    points.ack(10);
    for (int i=120;i<130;++i) assert(points.push(i));
    for (int i=0;i<120;++i) assert(points.at(i)==i+10);
    // Producer appends cannot move the snapshot's oldest point.
    points.ack(10);
    assert(points.push(130));
    assert(points.at(0)==20);
    points.ack(10);
    assert(points.at(0)==30 && points.size()==101);
    points.ack(points.size());
    assert(points.size()==0);
    std::cout<<"PASS: 120 slots, full backpressure, failed-batch retention, FIFO wrap, append during upload, ACK removal\n";
}
