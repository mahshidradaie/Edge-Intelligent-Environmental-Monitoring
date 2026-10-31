// Host-side unit tests for anomaly.h and ring_buffer.h:  g++ -std=c++11 -I../include test_core.cpp
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include "anomaly.h"
#include "ring_buffer.h"

using namespace edgeguard;
static uint32_t rng = 12345;
static float noise(float amp) {   // deterministic uniform noise in [-amp, amp]
    rng = rng * 1664525u + 1013904223u;
    return ((rng >> 8) / 16777216.0f * 2.0f - 1.0f) * amp;
}
#define CHECK(c, msg) do { if (!(c)) { std::printf("FAIL: %s\n", msg); std::exit(1); } } while (0)

int main() {
    // 1) no false alarms on 2000 clean samples
    AnomalyDetector d;
    for (int i = 0; i < 2000; ++i) CHECK(!d.update(22.0f + noise(0.15f)), "false positive on clean signal");
    std::puts("ok: no false positives over 2000 clean samples");

    // 2) a +3 C step is flagged within raiseCount samples and cleared afterwards
    int latency = -1;
    for (int i = 0; i < 20; ++i) if (d.update(25.0f + noise(0.15f)) && latency < 0) latency = i + 1;
    CHECK(latency == 3, "alert should rise after exactly 3 consecutive outliers");
    std::printf("ok: alert raised after %d samples\n", latency);
    int clear = -1;
    for (int i = 0; i < 20; ++i) if (!d.update(22.0f + noise(0.15f)) && clear < 0) clear = i + 1;
    CHECK(clear == 5, "alert should clear after exactly 5 normal samples");
    std::printf("ok: alert cleared after %d samples\n", clear);

    // 3) baseline is not poisoned by the incident
    CHECK(d.mean() > 21.8f && d.mean() < 22.2f, "baseline drifted during incident");
    std::puts("ok: baseline unchanged by incident");

    // 4) ring buffer: FIFO order and overwrite-oldest behaviour
    RingBuffer<int, 4> rb;
    for (int i = 1; i <= 6; ++i) rb.push(i);          // keeps 3,4,5,6
    int v, expect = 3;
    while (rb.pop(v)) CHECK(v == expect++, "ring buffer order");
    CHECK(expect == 7, "ring buffer overwrite-oldest");
    std::puts("ok: ring buffer FIFO + overwrite");
    std::puts("ALL TESTS PASSED");
    return 0;
}
