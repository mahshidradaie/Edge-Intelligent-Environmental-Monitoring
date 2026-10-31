// ring_buffer.h - fixed-capacity FIFO used to hold readings while the broker is unreachable.
// When full, the oldest entry is overwritten (newest data is the most valuable).
#pragma once
#include <stddef.h>

namespace edgeguard {

template <typename T, size_t CAP>
class RingBuffer {
public:
    void push(const T &v) {
        buf_[head_] = v;
        head_ = (head_ + 1) % CAP;
        if (size_ == CAP) tail_ = (tail_ + 1) % CAP; else ++size_;
    }
    bool pop(T &out) {
        if (size_ == 0) return false;
        out = buf_[tail_];
        tail_ = (tail_ + 1) % CAP;
        --size_;
        return true;
    }
    size_t size() const  { return size_; }
    bool   empty() const { return size_ == 0; }

private:
    T      buf_[CAP];
    size_t head_ = 0, tail_ = 0, size_ = 0;
};

}  // namespace edgeguard
