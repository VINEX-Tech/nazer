#pragma once
#include <cstddef>

// Synchronization belongs to the caller. Appending never moves the oldest
// point, so an HTTP snapshot stays valid until the worker acknowledges it.
template<class T, size_t Capacity> class PointRing {
public:
    size_t size() const { return count; }
    bool full() const { return count==Capacity; }
    const T& at(size_t offset) const { return points[(head+offset)%Capacity]; }
    bool push(const T& point) {
        if (full()) return false;
        points[(head+count)%Capacity]=point;
        if (!(points[(head+count)%Capacity]==point)) return false;
        ++count;
        return true;
    }
    void ack(size_t n) {
        while (n-- && count) {
            points[head]=T();
            head=(head+1)%Capacity;
            --count;
        }
    }
private:
    T points[Capacity];
    size_t head=0, count=0;
};
